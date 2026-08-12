#include "SettingsIni.h"

#include "Logging/Log.h"

#include <filesystem>

namespace SettingsIni
{
inih::INIReader LoadOrEmpty(const std::string& path, std::string_view logLabel)
{
    inih::INIReader ini;
    if (!std::filesystem::exists(path))
        return ini;

    try
    {
        ini = inih::INIReader(path);
    }
    catch (const std::exception& e)
    {
        LOG_WARN("{}: failed to read existing '{}' before merging: {} - other panels' settings may be lost", logLabel, path, e.what());
    }

    return ini;
}

void SaveMerged(const std::string& path, const inih::INIReader& ini, std::string_view logLabel)
{
    try
    {
        inih::INIWriter::write(path, ini, /*overwrite=*/true);
        LOG_INFO("{}: wrote settings to {}", logLabel, path);
    }
    catch (const std::exception& e)
    {
        LOG_ERROR("{}: failed to write '{}': {}", logLabel, path, e.what());
    }
}
}  // namespace SettingsIni
