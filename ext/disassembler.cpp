#include "disassembler.h"
#include <capstone/capstone.h>
#include <mutex>
#include "log.h"

namespace ext {

void DisassembleIter(const uint8_t* code, uint32_t address, size_t size, std::function<void(uint32_t, const std::string&, const std::string&)> callback)
{
    static std::once_flag flag;
    static csh handle;
    std::call_once(flag, [] () {
        if (cs_open(CS_ARCH_ARM, CS_MODE_THUMB, &handle) != CS_ERR_OK) {
            LogError("Disassembler: capstone initialize failed");
            std::terminate();
        }
    });

    uint64_t addr = address;
    cs_insn *insn = cs_malloc(handle);
    while(cs_disasm_iter(handle, &code, &size, &addr, insn)) {
        callback(insn->address, insn->mnemonic, insn->op_str);
    }
}

bool IsCodeExit(const std::string& mnemonic, const std::string& op)
{
    if (mnemonic == "bx" && op == "lr") {
        return true;
    } else if ((mnemonic == "pop" || mnemonic == "pop.w") && op.find("pc") != std::string::npos) {
        return true;
    }
    return false;
}

bool IsCodeDisableInterrupt(const std::string& mnemonic, const std::string& op)
{
    return mnemonic == "cpsid" && op == "i";
}

bool IsCodeEnableInterrupt(const std::string& mnemonic, const std::string& op)
{
    return mnemonic == "cpsie" && op == "i";
}

} // namespace ext
