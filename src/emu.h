#pragma once

#include <map>
#include <mutex>
#include <functional>
#include "io.h"

namespace m8 {

class CoreCallbacks : public Dynarmic::A32::UserCallbacks {
public:
    CoreCallbacks();
    void BindDevice(Device* dev);

    void Lock();
    void Unlock();
    void MemoryRead(u32 addr, void* buffer, int length);
    void MemoryWrite(u32 addr, void* buffer, int length);

    void PreCodeTranslationHook(bool is_thumb, u32 pc, Dynarmic::A32::IREmitter& ir) override;

    u8 MemoryRead8(u32 vaddr) override;
    u16 MemoryRead16(u32 vaddr) override;
    u32 MemoryRead32(u32 vaddr) override;
    u64 MemoryRead64(u32 vaddr) override;

    void MemoryWrite8(u32 vaddr, u8 value) override;
    void MemoryWrite16(u32 vaddr, u16 value) override;
    void MemoryWrite32(u32 vaddr, u32 value) override;
    void MemoryWrite64(u32 vaddr, u64 value) override;

    bool MemoryWriteExclusive32(u32 vaddr, u32 value, u32 expected) override;

    void* MemoryMap(u32 addr);

    std::array<u8*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>& PageTable() { return *pageTable; }

    void AddReadHook(u32 addr, std::function<u32(u32)> hook) { readHooks[addr] = hook; }
    void AddWriteHook(u32 addr, std::function<void(u32, u32)> hook) { writeHooks[addr] = hook; }
    void AddTranslationHook(u32 addr, std::function<void(u32, Dynarmic::A32::IREmitter&)> hook) { translationHooks[addr] = hook; }

    void InterpreterFallback(u32 pc, size_t num_instructions) override;
    void CallSVC(u32 swi) override;
    void ExceptionRaised(u32 pc, Dynarmic::A32::Exception exception) override;
    void AddTicks(u64 ticks) override;
    u64 GetTicksRemaining() override;

protected:
    std::recursive_mutex mutex;
    std::shared_ptr<std::array<u8*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>> pageTable;
    std::map<u32, Device*> devices;
    std::map<u32, std::function<u32(u32)>> readHooks;
    std::map<u32, std::function<void(u32, u32)>> writeHooks;
    std::map<u32, std::function<void(u32, Dynarmic::A32::IREmitter&)>> translationHooks;
};

} // namespace m8
