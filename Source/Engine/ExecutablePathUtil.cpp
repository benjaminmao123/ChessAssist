#include "ExecutablePathUtil.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

#include <vector>

namespace ExecutablePathUtil
{
#ifdef _WIN32
std::filesystem::path GetCurrentExecutablePath()
{
    std::vector<wchar_t> buffer(MAX_PATH);

    for (;;) {
        const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));

        if (length == 0)
            return {};

        if (length < buffer.size())
            return std::filesystem::path(buffer.data(), buffer.data() + length);

        buffer.resize(buffer.size() * 2);
    }
}

constexpr const wchar_t* kStockfishExeName = L"stockfish.exe";
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
}  // namespace ExecutablePathUtil
