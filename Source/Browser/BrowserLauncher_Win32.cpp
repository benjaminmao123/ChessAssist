#include "BrowserLauncher.h"

#define NOMINMAX
#include <windows.h>

#include <array>
#include <system_error>

namespace
{
// Chrome registers its install location here regardless of which drive/folder it was
// installed to - more reliable than guessing Program Files paths.
std::filesystem::path FindChromeExecutable()
{
    for (HKEY root : {HKEY_LOCAL_MACHINE, HKEY_CURRENT_USER})
    {
        HKEY key = nullptr;
        if (RegOpenKeyExW(root, LR"(SOFTWARE\Microsoft\Windows\CurrentVersion\App Paths\chrome.exe)", 0, KEY_READ, &key) != ERROR_SUCCESS)
            continue;

        std::array<wchar_t, MAX_PATH> buffer{};
        DWORD size = static_cast<DWORD>(buffer.size() * sizeof(wchar_t));
        const LSTATUS status = RegQueryValueExW(key, nullptr, nullptr, nullptr, reinterpret_cast<LPBYTE>(buffer.data()), &size);
        RegCloseKey(key);

        if (status == ERROR_SUCCESS)
            return std::filesystem::path(buffer.data());
    }

    static constexpr const wchar_t* kFallbackPaths[] = {
        LR"(C:\Program Files\Google\Chrome\Application\chrome.exe)",
        LR"(C:\Program Files (x86)\Google\Chrome\Application\chrome.exe)",
    };

    for (const wchar_t* path : kFallbackPaths)
    {
        if (std::filesystem::exists(path))
            return path;
    }

    return {};
}
}  // namespace

struct BrowserLauncher::Impl
{
    PROCESS_INFORMATION ProcessInfo{};
    bool Running = false;

    ~Impl()
    {
        Cleanup();
    }

    void Cleanup()
    {
        if (Running)
        {
            if (WaitForSingleObject(ProcessInfo.hProcess, 500) != WAIT_OBJECT_0)
                TerminateProcess(ProcessInfo.hProcess, 1);

            WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
            Running = false;
        }

        if (ProcessInfo.hProcess)
        {
            CloseHandle(ProcessInfo.hProcess);
            ProcessInfo.hProcess = nullptr;
        }

        if (ProcessInfo.hThread)
        {
            CloseHandle(ProcessInfo.hThread);
            ProcessInfo.hThread = nullptr;
        }
    }
};

BrowserLauncher::BrowserLauncher()
    : m_Impl(std::make_unique<Impl>())
{
}

BrowserLauncher::~BrowserLauncher() = default;

std::expected<void, BrowserError> BrowserLauncher::Launch(std::uint16_t port, const std::filesystem::path& profileDir, const std::string& startUrl)
{
    const std::filesystem::path chromePath = FindChromeExecutable();
    if (chromePath.empty())
        return std::unexpected(BrowserError{"Could not locate chrome.exe - is Chrome installed?"});

    std::error_code ec;
    std::filesystem::create_directories(profileDir, ec);

    std::wstring commandLine = L"\"" + chromePath.wstring() + L"\"";
    commandLine += L" --remote-debugging-port=" + std::to_wstring(port);
    commandLine += L" --user-data-dir=\"" + profileDir.wstring() + L"\"";
    commandLine += L" --no-first-run --no-default-browser-check";
    // Without this, Chrome's DevTools anti-DNS-rebinding check rejects the WebSocket upgrade
    // from any non-browser client with HTTP 403 - needed for a CDP client like this one to connect.
    commandLine += L" --remote-allow-origins=*";
    // Chrome throttles occluded/minimized windows (Windows' Native Window Occlusion tracker in
    // particular), which can stall the renderer's input hit-testing enough for CdpClient's
    // Input.dispatchMouseEvent calls (used to play moves) to time out - these opt this instance
    // out of that throttling so autoplay keeps working while the window isn't focused/visible.
    commandLine += L" --disable-backgrounding-occluded-windows --disable-renderer-backgrounding "
                   L"--disable-background-timer-throttling --disable-features=CalculateNativeWinOcclusion";
    if (!startUrl.empty())
        commandLine += L" " + std::wstring(startUrl.begin(), startUrl.end());

    std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);

    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        nullptr,
        commandLineBuffer.data(),
        nullptr,
        nullptr,
        FALSE,
        0,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);

    if (!created)
        return std::unexpected(BrowserError{"CreateProcessW failed with error " + std::to_string(GetLastError())});

    m_Impl->ProcessInfo = processInfo;
    m_Impl->Running = true;

    return {};
}

bool BrowserLauncher::IsRunning() const
{
    if (!m_Impl->Running)
        return false;

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(m_Impl->ProcessInfo.hProcess, &exitCode) || exitCode != STILL_ACTIVE)
    {
        m_Impl->Running = false;
        return false;
    }

    return true;
}

void BrowserLauncher::Terminate()
{
    m_Impl->Cleanup();
}
