#pragma once

#include <spdlog/common.h>

#include <imgui.h>

#include <mutex>
#include <string_view>

// In-app log view. AddLine() (via ImGuiLogSink) is safe to call from any thread; Draw() renders
// it each frame from the UI thread only. Both are synchronized via the same mutex - Draw() holds
// the lock for its whole body rather than copying the buffer out first, since the buffer is
// capped (see kMaxLines), so a full per-frame copy would still be the more expensive option.
class LogPanel
{
public:
    LogPanel();

    void AddLine(spdlog::level::level_enum level, std::string_view line);

    // Not thread-safe: call once per frame from the UI thread only. No-ops (doesn't even call
    // ImGui::Begin()) while closed - see SetOpen().
    void Draw();

    // Writes this panel's own settings (currently just whether it's open) to settings.ini,
    // overwriting whatever was there - same read-merge pattern as ControlsPanel::SaveSettings().
    // Called once by App at shutdown.
    void SaveSettings() const;

    // Shows/hides the panel - bound to the window's own titlebar close button (via Draw()'s
    // ImGui::Begin(..., &m_Open)) and to App's menu-bar checkbox for reopening it.
    [[nodiscard]] bool IsOpen() const { return m_Open; }
    void SetOpen(bool open) { m_Open = open; }

private:
    // Restores m_Open from settings.ini, if it exists. Called once from the constructor.
    void LoadSettings();

    // Draw() walks every retained line each frame (word-wrapped lines rule out ImGuiListClipper's
    // fixed-row-height fast path), so an uncapped buffer would make per-frame cost grow forever.
    // Trimmed in batches (down to kMaxLines once past kTrimThreshold) so compaction stays a rare
    // O(n) operation rather than happening on every AddLine call.
    static constexpr int kMaxLines = 5000;
    static constexpr int kTrimThreshold = kMaxLines + 1000;

    void TrimIfNeeded();  // caller must hold m_Mutex

    // See IsOpen()/SetOpen().
    bool m_Open = true;

    std::mutex m_Mutex;
    ImGuiTextBuffer m_Buffer;
    ImVector<int> m_LineOffsets;  // byte offset (into m_Buffer) where each line starts
    ImVector<int> m_LineLevels;   // spdlog::level::level_enum for the line at the same index
    ImGuiTextFilter m_Filter;
    bool m_AutoScroll = true;
    bool m_Selectable = false;

    // Consumed by Draw()'s Selectable branch (InputTextMultiline doesn't auto-follow new content
    // the way the normal clipped view does) - starts true so the first draw opens at the latest
    // message, and is re-armed by AddLine() while Auto-scroll is checked.
    bool m_ScrollSelectableToBottomPending = true;
};
