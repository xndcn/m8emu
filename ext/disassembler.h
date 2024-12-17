#pragma once

#include <string>
#include <functional>

namespace ext {

void DisassembleIter(const uint8_t* code, uint32_t address, size_t size, std::function<void(uint32_t, const std::string&, const std::string&)> callback);
bool IsCodeExit(const std::string&, const std::string&);

bool IsCodeDisableInterrupt(const std::string&, const std::string&);
bool IsCodeEnableInterrupt(const std::string&, const std::string&);

} // namespace ext
