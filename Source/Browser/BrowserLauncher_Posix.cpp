#include "BrowserLauncher.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace
{
std::filesystem::path FindChromeExecutable()
{
    static constexpr const char* kCandidates[] = {
        "/usr/bin/google-chrome",
        "/usr/bin/google-chrome-stable",
        "/usr/bin/chromium",
        "/usr/bin/chromium-browser",
        "/snap/bin/chromium",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
        "/Applications/Chromium.app/Contents/MacOS/Chromium",
    };

    for (const char* candidate : kCandidates)
    {
        if (std::filesystem::exists(candidate))
            return candidate;
    }

    return {};
}
}  // namespace

struct BrowserLauncher::Impl
{
    pid_t Pid = -1;
    bool Running = false;

    ~Impl()
    {
        Cleanup();
    }

    void Cleanup()
    {
        if (!Running)
            return;

        kill(Pid, SIGTERM);

        int status = 0;
        bool exited = false;

        for (int i = 0; i < 50; ++i)
        {
            if (waitpid(Pid, &status, WNOHANG) != 0)
            {
                exited = true;
                break;
            }

            usleep(10000);  // 10ms
        }

        if (!exited)
        {
            kill(Pid, SIGKILL);
            waitpid(Pid, &status, 0);
        }

        Running = false;
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
        return std::unexpected(BrowserError{"Could not locate a Chrome/Chromium executable"});

    std::error_code ec;
    std::filesystem::create_directories(profileDir, ec);

    std::string executablePathStr = chromePath.string();
    std::string portArg = "--remote-debugging-port=" + std::to_string(port);
    std::string profileArg = "--user-data-dir=" + profileDir.string();
    std::string noFirstRunArg = "--no-first-run";
    std::string noDefaultBrowserArg = "--no-default-browser-check";
    // Without this, Chrome's DevTools anti-DNS-rebinding check rejects the WebSocket
    // upgrade from any client that isn't itself a browser tab (HTTP 403 on the handshake) -
    // this flag explicitly allows a non-browser CDP client like this one to connect.
    std::string allowOriginsArg = "--remote-allow-origins=*";

    std::vector<char*> argv;
    argv.push_back(executablePathStr.data());
    argv.push_back(portArg.data());
    argv.push_back(profileArg.data());
    argv.push_back(noFirstRunArg.data());
    argv.push_back(noDefaultBrowserArg.data());
    argv.push_back(allowOriginsArg.data());

    std::string urlArg = startUrl;
    if (!urlArg.empty())
        argv.push_back(urlArg.data());

    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawnResult = posix_spawn(&pid, executablePathStr.c_str(), nullptr, nullptr, argv.data(), environ);

    if (spawnResult != 0)
        return std::unexpected(BrowserError{std::string("posix_spawn failed: ") + std::strerror(spawnResult)});

    m_Impl->Pid = pid;
    m_Impl->Running = true;

    return {};
}

bool BrowserLauncher::IsRunning() const
{
    if (!m_Impl->Running)
        return false;

    int status = 0;
    if (waitpid(m_Impl->Pid, &status, WNOHANG) != 0)
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
