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
    return true;
}

u32 FirmwareConfig::GetSymbolAddress(const std::string& symbol)
{
    return symbols[symbol];
}

} // namespace m8
