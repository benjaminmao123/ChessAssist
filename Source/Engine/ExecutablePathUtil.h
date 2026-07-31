#pragma once

#include <filesystem>

namespace ExecutablePathUtil
{
std::filesystem::path GetCurrentExecutablePath();
std::filesystem::path GetDefaultStockfishPath();
}  // namespace ExecutablePathUtil
