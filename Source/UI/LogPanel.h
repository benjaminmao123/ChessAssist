#pragma once

#include <spdlog/common.h>

#include <imgui.h>

#include <mutex>
#include <string_view>

// In-app log view. AddLine() is called from whatever thread logs (via ImGuiLogSink, see
// ImGuiLogSink.h) and is safe to call from any thread; Draw() renders it each frame from the
// UI thread only. Both are internally synchronized against each other - Draw() holds the
// lock for its whole body rather than copying the buffer out first, since the buffer is
// capped (see kMaxLines) rather than truly unbounded, so a full per-frame copy would still be
// the more expensive option. The full session log always remains available in the on-disk
// log file (see main.cpp) regardless of what's been trimmed from this in-app view.
class LogPanel
{
public:
    void AddLine(spdlog::level::level_enum level, std::string_view line);

    void Draw();

private:
    // Draw() walks every retained line each frame (word-wrapped lines rule out
    // ImGuiListClipper's fixed-row-height fast path - see Draw()), so an uncapped buffer
    // would make per-frame cost, and memory use, grow for as long as the app stays open.
    // Trimmed in batches (down to kMaxLines once past kTrimThreshold) so compaction stays a
    // rare O(n) operation rather than happening on every single AddLine call.
    static constexpr int kMaxLines = 5000;
    static constexpr int kTrimThreshold = kMaxLines + 1000;

    void TrimIfNeeded();  // caller must hold m_Mutex

    std::mutex m_Mutex;
    ImGuiTextBuffer m_Buffer;
    ImVector<int> m_LineOffsets;  // byte offset (into m_Buffer) where each line starts
    ImVector<int> m_LineLevels;   // spdlog::level::level_enum for the line at the same index
    ImGuiTextFilter m_Filter;
    bool m_AutoScroll = true;
    bool m_Selectable = false;
};
