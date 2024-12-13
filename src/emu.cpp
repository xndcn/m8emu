#include <algorithm>
#include <atomic>
#include "emu.h"
#include <ext/log.h>
#include <ext/ir.h>

namespace m8 {

CoreCallbacks::CoreCallbacks()
{
    pageTable = std::make_shared<std::array<u8*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>>();
}

void CoreCallbacks::BindDevice(Device* dev)
{
    devices.emplace(dev->BaseAddress(), dev);
    devices.emplace(dev->EndAddress(), dev);
    dev->UpdatePageTable(*pageTable);
}

void CoreCallbacks::Lock()
{
    mutex.lock();
}

void CoreCallbacks::Unlock()
{
    mutex.unlock();
}

void CoreCallbacks::PreCodeTranslationHook(bool is_thumb, u32 pc, Dynarmic::A32::IREmitter& ir)
{
    auto iter = translationHooks.find(pc);
    if (iter != translationHooks.end()) {
        iter->second(pc, ir);
    }
}

static Device* get_device(u32 addr, const std::map<u32, Device*>& devices)
{
    auto iter = devices.lower_bound(addr);
    if (iter != devices.end()) {
        Device* dev = iter->second;
        if (addr >= dev->BaseAddress() && addr <= dev->EndAddress()) {
            return dev;
        }
    }
    ext::LogDebug("get_device nullptr: addr = 0x%x", addr);
    return nullptr;
}

void CoreCallbacks::MemoryRead(u32 addr, void* buffer, int length)
{
    Device* dev = get_device(addr, devices);
    if (dev) {
        u32 offset = addr - dev->BaseAddress();
        dev->Read(offset, buffer, length);
    }
}

void CoreCallbacks::MemoryWrite(u32 addr, void* buffer, int length)
{
    Device* dev = get_device(addr, devices);
    if (dev) {
        u32 offset = addr - dev->BaseAddress();
        dev->Write(offset, buffer, length);
    }
}

u8 CoreCallbacks::MemoryRead8(u32 vaddr)
{
    u8 value;
    MemoryRead(vaddr, &value, sizeof(value));
    return value;
}

u16 CoreCallbacks::MemoryRead16(u32 vaddr)
{
    u16 value;
    MemoryRead(vaddr, &value, sizeof(value));
    return value;
}

u32 CoreCallbacks::MemoryRead32(u32 vaddr)
{
    if (readHooks.find(vaddr) != readHooks.end()) {
        return readHooks[vaddr](vaddr);
    } else {
        Device* dev = get_device(vaddr, devices);
        if (!dev) {
            return 0;
        } else {
            u32 offset = vaddr - dev->BaseAddress();
            return dev->Read32(offset);
        }
    }
}

u64 CoreCallbacks::MemoryRead64(u32 vaddr)
{
    u64 value;
    MemoryRead(vaddr, &value, sizeof(value));
    return value;
}

void CoreCallbacks::MemoryWrite8(u32 vaddr, u8 value)
{
    MemoryWrite(vaddr, &value, sizeof(value));
}

void CoreCallbacks::MemoryWrite16(u32 vaddr, u16 value)
{
    MemoryWrite(vaddr, &value, sizeof(value));
}

void CoreCallbacks::MemoryWrite32(u32 vaddr, u32 value)
{
    if (writeHooks.find(vaddr) != writeHooks.end()) {
        writeHooks[vaddr](vaddr, value);
    } else {
        Device* dev = get_device(vaddr, devices);
        if (dev) {
            u32 offset = vaddr - dev->BaseAddress();
            dev->Write32(offset, value);
        }
    }
}

void CoreCallbacks::MemoryWrite64(u32 vaddr, u64 value)
{
    MemoryWrite(vaddr, &value, sizeof(value));
}

bool CoreCallbacks::MemoryWriteExclusive32(u32 vaddr, u32 value, u32 expected)
{
    auto atomic = (std::atomic<u32>*)MemoryMap(vaddr);
    return atomic->compare_exchange_strong(expected, value);
}

void* CoreCallbacks::MemoryMap(u32 addr)
{
    Device* dev = get_device(addr, devices);
    if (!dev) {
        return nullptr;
    } else {
        u32 offset = addr - dev->BaseAddress();
        return dev->Map(offset);
    }
}

void CoreCallbacks::InterpreterFallback(u32 pc, size_t num_instructions)
{
}

void CoreCallbacks::CallSVC(u32 swi)
{
}

void CoreCallbacks::ExceptionRaised(u32 pc, Dynarmic::A32::Exception exception)
{
    ext::LogError("ExceptionRaised: pc = 0x%x, exception = %d", pc, (int)exception);
    std::terminate();
}

void CoreCallbacks::AddTicks(u64 ticks)
{
}

u64 CoreCallbacks::GetTicksRemaining()
{
    return 1;
}

} // namespace m8
