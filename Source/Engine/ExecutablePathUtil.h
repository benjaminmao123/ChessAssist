#pragma once

#include <filesystem>
#include <optional>

namespace ExecutablePathUtil
{
std::filesystem::path GetCurrentExecutablePath();
std::filesystem::path GetDefaultStockfishPath();
std::filesystem::path GetAssetsDirectory();
std::filesystem::path GetLogsDirectory();
std::filesystem::path GetBrowserProfileDirectory();
std::filesystem::path GetSettingsFilePath();

// Opens a native "choose a file" dialog (filtered to .exe) for picking a UCI engine
// executable - requires NFD_Init() to have been called first (see main.cpp) and NFD_Quit()
// at shutdown. Returns nullopt if the user cancelled or the dialog failed.
std::optional<std::filesystem::path> PromptForEnginePath();
}  // namespace ExecutablePathUtil
