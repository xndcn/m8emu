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
    template<class T> T GetValue(const std::string& key);
    u32 GetSymbolAddress(const std::string& symbol);
    std::tuple<u32, u32> GetEntryRange(const std::string& entry);

private:
    FirmwareConfig();

    std::map<std::string, u32> symbols;
    std::map<std::string, std::tuple<u32, u32>> ranges;

    std::shared_ptr<YAML::Node> config;
};

} // namespace m8
