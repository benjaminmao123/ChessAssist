#pragma once

#include <memory>
#include <string>

class AppWindow
{
public:
    AppWindow();
    ~AppWindow();
    AppWindow(const AppWindow&) = delete;
    AppWindow& operator=(const AppWindow&) = delete;

    [[nodiscard]] bool Init(int width, int height, const std::string& title);
    void Shutdown();

    [[nodiscard]] bool ShouldClose() const;

    // Iconifies/restores the window (e.g. to get it out of the way before a full-screen
    // capture). Safe to call before Init() or after Shutdown() - no-ops in that case.
    void Minimize();
    void Restore();

    // Polls window/input events and starts a new ImGui frame. Call once per iteration
    // before drawing any panels. Returns the main viewport dockspace's ID (really an
    // ImGuiID/unsigned int - kept as a plain unsigned int here so this header doesn't need
    // to include imgui.h) so callers can build a default dock layout with it via ImGui's
    // DockBuilder API (see App::Run()).
    [[nodiscard]] unsigned int BeginFrame();

    // Renders ImGui draw data and swaps buffers. Call once per iteration after drawing.
    void EndFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
