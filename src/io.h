
#pragma once

#include "common.h"
#include <vector>
#include <map>
#include <functional>
#include "dynarmic/interface/A32/a32.h"

namespace m8 {

class Device {
public:
    Device(u32 baseAddr, u32 size) : baseAddress(baseAddr), size(size) {}

    void BindInterrupt(int q, std::function<void(int)> callback) { irq = q; interruptCallback = callback; }

    virtual void Read(u32 offset, void* buffer, u32 length) = 0;
    virtual void Write(u32 offset, void* buffer, u32 length) = 0;

    virtual u32 Read32(u32 offset) = 0;
    virtual void Write32(u32 offset, u32 value) = 0;

    virtual void* Map(u32 offset) { return nullptr; }

    virtual bool UpdatePageTable(std::array<u8*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>& table) { return false; }

    u32 BaseAddress() { return baseAddress; }
    u32 EndAddress() { return baseAddress + size - 1; }
    u32 Size() { return size; }

    void TriggerInterrupt() const { if (interruptCallback) interruptCallback(irq); }

protected:
    u32 baseAddress;
    u32 size;
    std::function<void(int)> interruptCallback;
    int irq;
};

class MemoryDevice : public Device {
public:
    MemoryDevice(u32 baseAddr, u32 size);

    void Read(u32 offset, void* buffer, u32 length) override;
    void Write(u32 offset, void* buffer, u32 length) override;

    u32 Read32(u32 offset) override;
    void Write32(u32 offset, u32 value) override;

    void* Map(u32 offset) override;

    bool UpdatePageTable(std::array<u8*, Dynarmic::A32::UserConfig::NUM_PAGE_TABLE_ENTRIES>& table) override;

protected:
    std::vector<u8> memory;
};

struct Field {
    int offset;
    int length;
    std::function<u32()> readCallback;
    std::function<void(u32)> writeCallback;
};

struct Register {
    u32 addr;
    std::function<void(u32)> writeCallback;
    std::map<std::string, Field> fields;

    u32 Read32();
    void Write32(u32 value);
};

class RegisterDevice : public Device {
public:
    using Device::Device;

    void Read(u32 offset, void* buffer, u32 length) override;
    void Write(u32 offset, void* buffer, u32 length) override;

    u32 Read32(u32 offset) override;
    void Write32(u32 offset, u32 value) override;

protected:
    void BindRegister(const Register& reg);

    std::map<u32, Register> registers;
};

} // namespace m8
