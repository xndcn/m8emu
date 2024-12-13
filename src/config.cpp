#include "config.h"
#include <yaml-cpp/yaml.h>
#include <filesystem>
#include <ext/log.h>

namespace m8 {

FirmwareConfig& FirmwareConfig::GlobalConfig()
{
    static FirmwareConfig& config = *new FirmwareConfig();
    return config;
}

FirmwareConfig::FirmwareConfig()
{
    config = std::make_shared<YAML::Node>();
}

bool FirmwareConfig::LoadConfig(const std::string& path, const std::string& firmware)
{
    if (path.empty()) {
        #include "firmware.yaml.h"
        *config = YAML::Load(firmware_yaml);
    } else {
        *config = YAML::LoadFile(path);
    }
    // TODO: Add hash check here
    std::string firmware_name = std::filesystem::path(firmware).filename();
    if (!(*config)[firmware_name]) {
        ext::LogError("Failed to parse firmware %s", firmware_name.c_str());
        return false;
    }
    *config = (*config)[firmware_name];

    for (const auto& symbol : (*config)["symbols"]) {
        const auto& name = symbol.first.as<std::string>();
        const auto& addr = symbol.second.as<u32>();
        symbols[name] = addr;
    }
    for (const auto& range : (*config)["ranges"]) {
        const auto& name = range.first.as<std::string>();
        ranges[name] = {range.second[0].as<u32>(), range.second[1].as<u32>()};
    }
    return true;
}

template<class T> T FirmwareConfig::GetValue(const std::string& key)
{
    return (*config)["configs"][key].as<T>();
}

template u32 FirmwareConfig::GetValue<u32>(const std::string& key);

u32 FirmwareConfig::GetSymbolAddress(const std::string& symbol)
{
    return symbols[symbol];
}

std::tuple<u32, u32> FirmwareConfig::GetEntryRange(const std::string& entry)
{
    return ranges[entry];
}

} // namespace m8
