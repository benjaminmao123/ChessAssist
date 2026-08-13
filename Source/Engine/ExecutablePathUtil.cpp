#include "ExecutablePathUtil.h"

#ifdef _WIN32
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#else
#include <unistd.h>
#endif

#include <cstdint>
#include <vector>
#include <nfd.h>

namespace ExecutablePathUtil
{
#ifdef _WIN32
std::filesystem::path GetCurrentExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;)
    {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length == 0)
            return {};

        if (length < buffer.size())
            return std::filesystem::path(buffer.data(), buffer.data() + length);

        buffer.resize(buffer.size() * 2);
    }
}

constexpr const wchar_t* kStockfishExeName = L"stockfish.exe";
#elif defined(__APPLE__)
std::filesystem::path GetCurrentExecutablePath()
{
    // macOS has no /proc, so the executable's own path has to come from dyld directly - the
    // first call (null buffer) reports the required size, then the second call fills it.
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);

    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0)
        return {};

    return std::filesystem::canonical(std::filesystem::path(buffer.data()));
}

constexpr const char* kStockfishExeName = "stockfish";
#else
std::filesystem::path GetCurrentExecutablePath()
{
    return std::filesystem::read_symlink("/proc/self/exe");
}

constexpr const char* kStockfishExeName = "stockfish";
#endif

std::filesystem::path GetDefaultStockfishPath()
{
    return GetCurrentExecutablePath().parent_path() / kStockfishExeName;
}

std::filesystem::path GetAssetsDirectory()
{
    return GetCurrentExecutablePath().parent_path() / "Assets";
}

std::filesystem::path GetLogsDirectory()
{
    return GetCurrentExecutablePath().parent_path() / "Logs";
}

std::filesystem::path GetBrowserProfileDirectory()
{
    return GetCurrentExecutablePath().parent_path() / "BrowserProfile";
}

std::filesystem::path GetSettingsFilePath()
{
    return GetCurrentExecutablePath().parent_path() / "settings.ini";
}

std::filesystem::path GetImGuiIniFilePath()
{
    return GetCurrentExecutablePath().parent_path() / "imgui.ini";
}

namespace
{
// Shared body behind PromptForEnginePath()/PromptForBookPath() - differ only in the dialog's
// filter name/extension.
std::optional<std::filesystem::path> PromptForFile(const char* filterName, const char* filterExtension)
{
    nfdchar_t* outPath = nullptr;
    nfdfilteritem_t filterItem = {filterName, filterExtension};

    const nfdresult_t result = NFD_OpenDialog(&outPath, &filterItem, 1, nullptr);

    if (result != NFD_OKAY)
        return std::nullopt;  // cancelled, or an error occurred

    std::filesystem::path path(outPath);
    free(outPath);
    return path;
}
}  // namespace

std::optional<std::filesystem::path> PromptForEnginePath()
{
    return PromptForFile("Executable Files", "exe");
}

std::optional<std::filesystem::path> PromptForBookPath()
{
    return PromptForFile("Polyglot Book", "bin");
}
}  // namespace ExecutablePathUtil
