#pragma once

#include <spdlog/common.h>

#include <imgui.h>

#include <mutex>
#include <string_view>

// Growing in-app log view. AddLine() is called from whatever thread logs (via
// ImGuiLogSink, see ImGuiLogSink.h) and is safe to call from any thread; Draw() renders it
// each frame from the UI thread only. Both are internally synchronized against each other -
// Draw() holds the lock for its whole body rather than copying the buffer out first, since
// the buffer only grows and a full per-frame copy would be the more expensive option.
class LogPanel
{
public:
    void AddLine(spdlog::level::level_enum level, std::string_view line);

    void Draw();

private:
    std::mutex m_Mutex;
    ImGuiTextBuffer m_Buffer;
    ImVector<int> m_LineOffsets;  // byte offset (into m_Buffer) where each line starts
    ImVector<int> m_LineLevels;   // spdlog::level::level_enum for the line at the same index
    ImGuiTextFilter m_Filter;
    bool m_AutoScroll = true;
    bool m_Selectable = false;
};
