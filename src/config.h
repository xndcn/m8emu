#pragma once

#include "common.h"
#include <string>
#include <map>
#include <tuple>
#include <memory>

namespace YAML {
class Node;
} // namespace YAML

namespace m8 {

class FirmwareConfig {
public:
    static FirmwareConfig& GlobalConfig();
    bool LoadConfig(const std::string& path, const std::string& firmware);
    u32 GetSymbolAddress(const std::string& symbol);

private:
    FirmwareConfig();

    std::map<std::string, u32> symbols;
    std::shared_ptr<YAML::Node> config;
};

} // namespace m8
