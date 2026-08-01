#pragma once

#include <filesystem>

namespace ExecutablePathUtil
{
std::filesystem::path GetCurrentExecutablePath();
std::filesystem::path GetDefaultStockfishPath();
std::filesystem::path GetAssetsDirectory();
std::filesystem::path GetLogsDirectory();
std::filesystem::path GetBrowserProfileDirectory();
}  // namespace ExecutablePathUtil
