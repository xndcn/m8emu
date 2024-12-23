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
    firmwareName = std::filesystem::path(firmware).filename();
    if (!(*config)[firmwareName]) {
        ext::LogError("Failed to parse firmware %s", firmwareName.c_str());
        return false;
    }

    for (const auto& symbol : (*config)[firmwareName]["symbols"]) {
        const auto& name = symbol.first.as<std::string>();
        const auto& addr = symbol.second.as<u32>();
        symbols[name] = addr;
    }
    for (const auto& range : (*config)[firmwareName]["ranges"]) {
        const auto& name = range.first.as<std::string>();
        ranges[name] = {range.second[0].as<u32>(), range.second[1].as<u32>()};
    }
    return true;
}

template<class T> T FirmwareConfig::GetValue(const std::string& key)
{
    if ((*config)[firmwareName]["configs"][key]) {
        return (*config)[firmwareName]["configs"][key].as<T>();
    } else if ((*config)["*"]["configs"][key]) {
        return (*config)["*"]["configs"][key].as<T>();
    }
    return {};
}

template u32 FirmwareConfig::GetValue<u32>(const std::string& key);
template bool FirmwareConfig::GetValue<bool>(const std::string& key);

u32 FirmwareConfig::GetSymbolAddress(const std::string& symbol)
{
    return symbols[symbol];
}

std::tuple<u32, u32> FirmwareConfig::GetEntryRange(const std::string& entry)
{
    return ranges[entry];
}

} // namespace m8
