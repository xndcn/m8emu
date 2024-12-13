#pragma once

#include "emu.h"
#include "io.h"
#include "timer.h"
#include "usb.h"
#include "dynarmic/interface/A32/config.h"
#include "dynarmic/interface/exclusive_monitor.h"
#include <memory>
#include <tuple>
#include <queue>
#include <mutex>
#include <condition_variable>

namespace m8 {

class M8Emulator {
public:
    M8Emulator();
    void LoadHEX(const char* hex_path);
    u32 Run();

    CoreCallbacks& Callbacks() { return callbacks; }
    u32 CallFunction(u32 addr, u32 param1);
    u32 VectorAddress(int interrupt) { return vectorTables[interrupt]; }

    void AttachInitializeCallback(std::function<void()> callback) { initializeCallbacks.push_back(callback); }

    m8::USBDevice& USBDevice() { return usb; }

private:
    void UpdateVectorTables(u32 addr);
    void TriggerInterrupt(int interrupt);
    void EnterInterrupt(int interrupt);
    void ExitInterrupt();
    std::shared_ptr<Dynarmic::A32::Jit> GetIdleJit();
    void SetJitIdle(const std::shared_ptr<Dynarmic::A32::Jit>& jit);

private:
    MemoryDevice itcm;
    MemoryDevice dtcm;
    MemoryDevice ocram2;
    MemoryDevice flash;
    MemoryDevice extraMemory;
    USB usb;
    u32 systick_millis_count = 0;
    u32 SNVS_LPCR = 0;
    u32* vectorTables = nullptr;
    bool inInterrupt = false;
    int inInterruptNumber = 0;
    std::tuple<std::array<std::uint32_t, 16>, uint32_t, uint32_t> backupRegs;
    std::map<int, bool> pendingInterrupts;
    std::mutex interruptMutex;
    Timer systick;
    std::mutex jitPoolMutex;
    std::condition_variable jitPoolIdle;
    std::vector<std::shared_ptr<Dynarmic::A32::Jit>> jitPool;
    std::map<std::shared_ptr<Dynarmic::A32::Jit>, bool> jitPoolRunning;
    std::map<std::shared_ptr<Dynarmic::A32::Jit>, int> jitPoolIndex;
    std::vector<std::function<void()>> initializeCallbacks;
    std::once_flag initializeFlag;

private:
    CoreCallbacks callbacks;
    std::shared_ptr<Dynarmic::A32::Jit> cpu;
    Dynarmic::A32::UserConfig config;
    Dynarmic::ExclusiveMonitor monitor;
};

} // namespace m8

