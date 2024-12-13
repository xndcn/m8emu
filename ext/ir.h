#pragma once
#include <cstdint>
#include <memory>

namespace Dynarmic::A32 {
class IREmitter;
}

namespace ext {

enum class Reg {
    R0 = 0,
    R1 = 1,
    R2 = 2,
    R3 = 3,
    R4 = 4,
    SP = 13,
    LR = 14,
    PC = 15,
};

struct U64 {
    U64();
    U64(std::uint64_t imm);
    U64(Dynarmic::A32::IREmitter& ir, Reg reg);

    std::shared_ptr<void> opaque;
};

void CallHostFunction(Dynarmic::A32::IREmitter& ir, void (*fn)(std::uint64_t), const U64& arg1);
void CallHostFunction(Dynarmic::A32::IREmitter& ir, void (*fn)(std::uint64_t, std::uint64_t), const U64& arg1, const U64& arg2);
void CallHostFunction(Dynarmic::A32::IREmitter& ir, void (*fn)(std::uint64_t, std::uint64_t, std::uint64_t), const U64& arg1, const U64& arg2, const U64& arg3);
void SetRegister(Dynarmic::A32::IREmitter& ir, Reg reg, std::uint32_t value);

} // namespace ext
