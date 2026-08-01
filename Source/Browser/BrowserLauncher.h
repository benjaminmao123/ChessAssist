#pragma once

#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <string>

struct BrowserError
{
    std::string Message;
};

// Spawns and owns a dedicated Chrome instance with remote debugging enabled, so CdpClient
// can talk to it. PIMPL + per-platform .cpp, same pattern as Process/ChildProcess - not
// built on ChildProcess itself, since that class's pipe-based stdin/stdout model exists for
// line-oriented protocols like UCI; Chrome is a GUI subprocess we just spawn and
// independently terminate, with no piped stdio to drain.
class BrowserLauncher
{
public:
    BrowserLauncher();
    ~BrowserLauncher();
    BrowserLauncher(const BrowserLauncher&) = delete;
    BrowserLauncher& operator=(const BrowserLauncher&) = delete;

    // Launches Chrome with --remote-debugging-port=port --user-data-dir=profileDir (a
    // non-default, app-managed profile - required since Chrome 136+ refuses the debugging
    // flag against the user's default profile) and navigates the initial tab to startUrl.
    [[nodiscard]] std::expected<void, BrowserError> Launch(std::uint16_t port, const std::filesystem::path& profileDir, const std::string& startUrl);

    [[nodiscard]] bool IsRunning() const;
    void Terminate();  // idempotent

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
