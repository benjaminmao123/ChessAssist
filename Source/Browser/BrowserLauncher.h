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
// can talk to it. Not built on ChildProcess since that class's pipe-based stdio model is for
// line-oriented protocols like UCI - Chrome is a GUI subprocess we just spawn and terminate.
class BrowserLauncher
{
public:
    BrowserLauncher();
    ~BrowserLauncher();
    BrowserLauncher(const BrowserLauncher&) = delete;
    BrowserLauncher& operator=(const BrowserLauncher&) = delete;

    // Launches Chrome with --remote-debugging-port=port --user-data-dir=profileDir (must be
    // non-default - Chrome 136+ refuses the debugging flag on the user's default profile) and
    // navigates the initial tab to startUrl.
    [[nodiscard]] std::expected<void, BrowserError> Launch(std::uint16_t port, const std::filesystem::path& profileDir, const std::string& startUrl);

    [[nodiscard]] bool IsRunning() const;
    void Terminate();  // idempotent

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
