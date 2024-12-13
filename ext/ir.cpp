#include "ir.h"
#include <dynarmic/frontend/A32/a32_ir_emitter.h>

namespace ext {

U64::U64()
{
    opaque = std::make_shared<Dynarmic::IR::U64>();
}

U64::U64(std::uint64_t imm)
{
    Dynarmic::IR::Value v(imm);
    opaque = std::make_shared<Dynarmic::IR::U64>(v);
}

U64::U64(Dynarmic::A32::IREmitter& ir, Reg reg)
{
    auto v = ir.GetRegister((Dynarmic::A32::Reg)reg);
    opaque = std::make_shared<Dynarmic::IR::U64>(ir.ZeroExtendWordToLong(v));
}

#define SELF (*((Dynarmic::IR::U64*)opaque.get()))
#define OPAQUE(x) (*((Dynarmic::IR::U64*)x.opaque.get()))

void CallHostFunction(Dynarmic::A32::IREmitter& ir, void (*fn)(std::uint64_t), const U64& arg1)
{
    ir.CallHostFunction(fn, OPAQUE(arg1));
}

void CallHostFunction(Dynarmic::A32::IREmitter& ir, void (*fn)(std::uint64_t, std::uint64_t), const U64& arg1, const U64& arg2)
{
    ir.CallHostFunction(fn, OPAQUE(arg1), OPAQUE(arg2));
}

void CallHostFunction(Dynarmic::A32::IREmitter& ir, void (*fn)(std::uint64_t, std::uint64_t, std::uint64_t), const U64& arg1, const U64& arg2, const U64& arg3)
{
    ir.CallHostFunction(fn, OPAQUE(arg1), OPAQUE(arg2), OPAQUE(arg3));
}

void SetRegister(Dynarmic::A32::IREmitter& ir, Reg reg, std::uint32_t value)
{
    auto v = ir.Imm32(value);
    ir.SetRegister((Dynarmic::A32::Reg)reg, v);
}

} // namespace ext
