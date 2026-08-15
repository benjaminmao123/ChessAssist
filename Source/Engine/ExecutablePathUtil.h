#pragma once

#include <filesystem>
#include <optional>

namespace ExecutablePathUtil
{
std::filesystem::path GetCurrentExecutablePath();
// Named for what it's for, not what it currently is - the bundled engine happens to be
// Stockfish today, but that could change without this API needing to.
std::filesystem::path GetDefaultEnginePath();

// The bundled Komodo Polyglot book, at Assets/Opening Books/komodo.bin - ControlsPanel
// pre-fills its book path field with this so a fresh install already has one available.
std::filesystem::path GetDefaultOpeningBookPath();

std::filesystem::path GetAssetsDirectory();
std::filesystem::path GetLogsDirectory();
std::filesystem::path GetBrowserProfileDirectory();
std::filesystem::path GetSettingsFilePath();

// Where Dear ImGui's window/dock layout is persisted (see AppWindow::Init()'s io.IniFilename) -
// next to the executable rather than ImGui's default "imgui.ini" (relative to the current
// working directory, not necessarily the exe's own).
std::filesystem::path GetImGuiIniFilePath();

// Opens a native "choose a file" dialog filtered to .exe - requires NFD_Init() to have been
// called first (see main.cpp) and NFD_Quit() at shutdown.
std::optional<std::filesystem::path> PromptForEnginePath();

// Same as PromptForEnginePath(), but filtered to .bin for picking a Polyglot opening book.
std::optional<std::filesystem::path> PromptForBookPath();
}  // namespace ExecutablePathUtil
