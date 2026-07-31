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

    // Polls window/input events and starts a new ImGui frame. Call once per iteration
    // before drawing any panels.
    void BeginFrame();

    // Renders ImGui draw data and swaps buffers. Call once per iteration after drawing.
    void EndFrame();

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
