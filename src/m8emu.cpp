#include "m8emu.h"
#include "kk_ihex_read.h"
#include <fstream>
#include <ext/log.h>
#include "config.h"

#define HEX_ENTRY    0x60001004
#define IRQ_HANDLER  0xFFFFFFF0

#define ITCM_BASE    0x00000000
#define ITCM_SIZE    (512 * 1024)
#define DTCM_BASE    0x20000000
#define DTCM_SIZE    (512 * 1024)
#define OCRAM2_BASE  0x20200000
#define OCRAM2_SIZE  (512 * 1024)
#define FLASH_BASE   0x60000000
#define FLASH_SIZE   (16 * 1024 * 1024)
#define USB_BASE     0x402E0000
#define USB_SIZE     0x00004000
#define USDHC1_BASE  0x402C0000
#define USDHC1_SIZE  0x00004000

#define SYSTICK_IRQ  15
#define USB_IRQ      (113 + 16)
#define USDHC1_IRQ   (110 + 16)

#define JIT_POOL_SIZE 6
#define JIT_MEM_SIZE (8 * 1024)
#define AUDIO_MEM_SIZE (256 * 1024)

#define EXTRA_MEM_BASE 0xB0000000
#define EXTRA_MEM_SIZE ((JIT_POOL_SIZE + 1) * JIT_MEM_SIZE + AUDIO_MEM_SIZE)
#define AUDIO_MEM_BASE EXTRA_MEM_BASE
#define JIT_MEM_BASE (AUDIO_MEM_BASE + AUDIO_MEM_SIZE)

using namespace std::chrono_literals;

namespace m8 {

const std::map<u32, u32> constValues = {
    {0x400D8010, 0x80003040}, // USB PLL
    {0x40080000, 1 << 31},    // DCDC_REG0_STS_DC_OK
    {0x400D8000, 1 << 31},    // CCM_ANALOG_PLL_ARM_LOCK
    {0x402A4014, 1 << 0},     // FLEXSPI_INTR_IPCMDDONE
    {0x402A8014, 1 << 0},
    {0x400D8070, 1 << 31},    // CCM_ANALOG_PLL_AUDIO_LOCK
    {0x400C4020, 1 << 0},     // ADC_HS_COCO0
    {0x400C8020, 1 << 0},     // ADC_HS_COCO0
};

M8Emulator::M8Emulator() :
    itcm(ITCM_BASE, ITCM_SIZE),
    dtcm(DTCM_BASE, DTCM_SIZE),
    ocram2(OCRAM2_BASE, OCRAM2_SIZE),
    flash(FLASH_BASE, FLASH_SIZE),
    extraMemory(EXTRA_MEM_BASE, EXTRA_MEM_SIZE),
    usb(callbacks, USB_BASE, USB_SIZE),
    sdhc(callbacks, USDHC1_BASE, USDHC1_SIZE),
    monitor(1)
{
    callbacks.BindDevice(&itcm);
    callbacks.BindDevice(&dtcm);
    callbacks.BindDevice(&ocram2);
    callbacks.BindDevice(&flash);
    callbacks.BindDevice(&extraMemory);
    callbacks.BindDevice(&usb);
    callbacks.BindDevice(&sdhc);
    usb.BindInterrupt(USB_IRQ, [this](int irq) { TriggerInterrupt(irq); });
    sdhc.BindInterrupt(USDHC1_IRQ, [this](int irq) { TriggerInterrupt(irq); });

    config.page_table = &callbacks.PageTable();
    config.callbacks = &callbacks;
    config.global_monitor = &monitor;
    // config.optimizations = Dynarmic::no_optimizations;
    cpu = std::make_shared<Dynarmic::A32::Jit>(config);
    cpu->SetCpsr(0x00000030); // Thumb mode

    auto constValuesHook = [](u32 addr) {
        return constValues.find(addr)->second;
    };
    for (const auto& [addr, v] : constValues) {
        callbacks.AddReadHook(addr, constValuesHook);
    }
    /* trick to exit interrupt */
    callbacks.AddReadHook(IRQ_HANDLER, [this](u32) { return 0x70477047; }); // bx lr
    callbacks.AddReadHook(0xE000E018, [this](u32) { return systick_millis_count; });
    callbacks.AddReadHook(0x400D4038, [this](u32) { return SNVS_LPCR; });
    callbacks.AddWriteHook(0x400D4038, [this](u32, u32 value) { SNVS_LPCR = value; });
    callbacks.AddWriteHook(0xE000ED08, [this](u32, u32 value) { UpdateVectorTables(value); });
    /* systick timer */
    callbacks.AddWriteHook(0xE000E010, [this](u32, u32 value) {
        if (value & 1) {
            systick.SetInterval(1ms, [this] (Timer&) { TriggerInterrupt(SYSTICK_IRQ); });
            systick.Start();
        } else {
            systick.Stop();
        }
    });

    jitPool.resize(JIT_POOL_SIZE);
    for (int i = 0; i < jitPool.size(); i++) {
        jitPool[i] = std::make_shared<Dynarmic::A32::Jit>(config);
        jitPoolRunning[jitPool[i]] = false;
        jitPoolIndex[jitPool[i]] = i;
    }

    auto setupDoneEntry = FirmwareConfig::GlobalConfig().GetSymbolAddress("setup_done");
    callbacks.AddTranslationHook(setupDoneEntry, [this, setupDoneEntry](u32, Dynarmic::A32::IREmitter& ir) {
        std::call_once(initializeFlag, [this, setupDoneEntry]() {
            for (auto& callback : initializeCallbacks) {
                callback();
            }
            ext::LogInfo("M8: setup initialized");
        });
    });
}

std::function<void(u32, void*, int)> memoryWriteCallback;

extern "C" ihex_bool_t ihex_data_read(struct ihex_state *ihex, ihex_record_type_t type, ihex_bool_t error)
{
    error = error || (ihex->length < ihex->line_length);
    if (type == IHEX_DATA_RECORD && !error) {
	    auto addr = IHEX_LINEAR_ADDRESS(ihex);
        memoryWriteCallback(addr, ihex->data, ihex->length);
    }
    return !error;
}

#define CURRENT_PC() cpu->Regs()[15]

void M8Emulator::LoadHEX(const char* hex_path)
{
    memoryWriteCallback = [this] (u32 addr, void* data, int length) {
        callbacks.MemoryWrite(addr, data, length);
    };
    struct ihex_state ihex;
    ihex_begin_read(&ihex);
    std::ifstream infile(hex_path);
    std::string line;
	while (std::getline(infile, line)) {
		ihex_read_bytes(&ihex, line.c_str(), line.size());
	}
    ihex_end_read(&ihex);

    u32 entry = callbacks.MemoryRead32(HEX_ENTRY);
    CURRENT_PC() = entry;
}

void M8Emulator::UpdateVectorTables(u32 addr)
{
    vectorTables = (u32*)callbacks.MemoryMap(addr);
    ext::LogInfo("NVIC: UpdateVectorTables 0x%x", addr);
}

void M8Emulator::TriggerInterrupt(int interrupt)
{
    std::lock_guard lock(interruptMutex);
    pendingInterrupts[interrupt] = true;
}

void M8Emulator::EnterInterrupt(int interrupt)
{
    inInterrupt = true;
    inInterruptNumber = interrupt;
    backupRegs = {cpu->Regs(), cpu->Cpsr(), cpu->Fpscr()};
    CURRENT_PC() = vectorTables[interrupt] & (~1);
    cpu->SetCpsr(0x00000030); // Thumb mode
    cpu->SetFpscr(0);
    cpu->Regs()[14] = IRQ_HANDLER; // LR
    ext::LogDebug("EnterInterrupt: irq = %d, pc = 0x%x", interrupt, CURRENT_PC());
}

void M8Emulator::ExitInterrupt()
{
    const auto& [regs, cpsr, fpscr] = backupRegs;
	cpu->Regs() = regs;
	cpu->SetCpsr(cpsr);
	cpu->SetFpscr(fpscr);
    inInterrupt = false;
    ext::LogDebug("ExitInterrupt: pc = 0x%x", CURRENT_PC());
}

u32 M8Emulator::Run()
{
    if (inInterrupt) {
        if (CURRENT_PC() == 0 || CURRENT_PC() >= IRQ_HANDLER) {
            ExitInterrupt();
        }
    } else {
        std::lock_guard lock(interruptMutex);
        for (auto [interrupt, triggered] : pendingInterrupts) {
            if (triggered) {
                EnterInterrupt(interrupt);
                pendingInterrupts[interrupt] = false;
                break;
            }
        }
    }

    callbacks.Lock();
    cpu->Run();
    callbacks.Unlock();

    return CURRENT_PC();
}

std::shared_ptr<Dynarmic::A32::Jit> M8Emulator::GetIdleJit()
{
    std::unique_lock lock(jitPoolMutex);
    jitPoolIdle.wait(lock, [this] {
        for (const auto& [jit, running] : jitPoolRunning) {
            if (!running) {
                return true;
            }
        }
        return false;
    });
    for (auto jit : jitPool) {
        bool running = jitPoolRunning[jit];
        if (!running) {
            jitPoolRunning[jit] = true;
            return jit;
        }
    }
    return nullptr;
}

void M8Emulator::SetJitIdle(const std::shared_ptr<Dynarmic::A32::Jit>& jit)
{
    std::lock_guard lock(jitPoolMutex);
    jitPoolRunning[jit] = false;
    jitPoolIdle.notify_one();
}

u32 M8Emulator::CallFunction(u32 addr, u32 param1)
{
    auto now = std::chrono::steady_clock::now();
    auto jit = GetIdleJit();
    jit->SetCpsr(0x00000030); // Thumb mode
    jit->SetFpscr(0);
    jit->Regs()[15] = addr & (~1);
    jit->Regs()[0] = param1;
    jit->Regs()[14] = IRQ_HANDLER;
    jit->Regs()[13] = JIT_MEM_BASE + JIT_MEM_SIZE * (jitPoolIndex[jit] + 1);

    int index = 0;
    bool step = false;
    while (jit->Regs()[15] != 0 && jit->Regs()[15] < IRQ_HANDLER) {
        jit->Run();
    }
    u32 result = jit->Regs()[0];
    SetJitIdle(jit);
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - now).count();
    ext::LogDebug("call function 0x%x result = 0x%x, duration = %d us", addr, result, duration);

    return result;
}

} // namespace m8
