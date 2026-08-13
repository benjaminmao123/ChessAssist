#include "LogPanel.h"

#include "Engine/ExecutablePathUtil.h"
#include "Logging/Log.h"
#include "Settings/SettingsIni.h"

#include <ini/ini.h>

#include <cfloat>
#include <filesystem>
#include <string>

namespace
{
ImVec4 LevelColor(spdlog::level::level_enum level)
{
    switch (level)
    {
    case spdlog::level::critical:
    case spdlog::level::err:
        return ImVec4(1.0f, 0.4f, 0.4f, 1.0f);
    case spdlog::level::warn:
        return ImVec4(1.0f, 0.85f, 0.4f, 1.0f);
    case spdlog::level::debug:
    case spdlog::level::trace:
        return ImVec4(0.6f, 0.6f, 0.6f, 1.0f);
    default:
        return ImGui::GetStyle().Colors[ImGuiCol_Text];
    }
}
}  // namespace

LogPanel::LogPanel()
{
    LoadSettings();
}

void LogPanel::LoadSettings()
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();
    if (!std::filesystem::exists(path))
        return;

    try
    {
        const inih::INIReader ini(path);
        // See BoardStatePanel::LoadSettings()'s comment for why the static_cast is needed here.
        m_Open = ini.Get<bool>("Window", "LogOpen", static_cast<bool>(m_Open));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("LogPanel::LoadSettings: failed to read '{}': {} - using defaults", path, e.what());
    }
}

void LogPanel::SaveSettings() const
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();

    // Read-merge rather than starting from a blank INIReader - same reasoning as
    // BoardStatePanel::SaveSettings()'s own comment.
    inih::INIReader ini = SettingsIni::LoadOrEmpty(path, "LogPanel::SaveSettings");

    SettingsIni::UpsertEntry(ini, "Window", "LogOpen", m_Open);

    SettingsIni::SaveMerged(path, ini, "LogPanel::SaveSettings");
}

void LogPanel::AddLine(spdlog::level::level_enum level, std::string_view line)
{
    std::scoped_lock lock(m_Mutex);

    m_LineOffsets.push_back(m_Buffer.size());
    m_LineLevels.push_back(static_cast<int>(level));

    m_Buffer.append(line.data(), line.data() + line.size());
    m_Buffer.append("\n");

    if (m_AutoScroll)
        m_ScrollSelectableToBottomPending = true;

    TrimIfNeeded();
}

void LogPanel::TrimIfNeeded()
{
    if (m_LineOffsets.Size <= kTrimThreshold)
        return;

    const int dropCount = m_LineOffsets.Size - kMaxLines;
    const int dropBytes = m_LineOffsets[dropCount];

    m_Buffer.Buf.erase(m_Buffer.Buf.begin(), m_Buffer.Buf.begin() + dropBytes);
    m_LineOffsets.erase(m_LineOffsets.begin(), m_LineOffsets.begin() + dropCount);
    m_LineLevels.erase(m_LineLevels.begin(), m_LineLevels.begin() + dropCount);

    for (int& offset : m_LineOffsets)
        offset -= dropBytes;
}

void LogPanel::Draw()
{
    if (!m_Open)
        return;

    std::scoped_lock lock(m_Mutex);

    ImGui::Begin("Log", &m_Open);

    if (ImGui::Button("Clear"))
    {
        m_Buffer.clear();
        m_LineOffsets.clear();
        m_LineLevels.clear();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Auto-scroll", &m_AutoScroll);
    ImGui::SameLine();
    ImGui::Checkbox("Selectable", &m_Selectable);
    ImGui::SameLine();
    m_Filter.Draw("Filter", -60.0f);

    ImGui::Separator();

    const char* bufStart = m_Buffer.begin();
    const char* bufEnd = m_Buffer.end();

    if (m_Selectable)
    {
        // TextUnformatted/Selectable don't support mouse-drag text selection in Dear ImGui; a
        // read-only InputTextMultiline does, letting you drag-select and Ctrl+C a snippet (e.g.
        // a FEN string) out of the log - traded off against the per-line coloring and auto-scroll
        // the normal clipped view below has.
        std::string filtered;
        const char* textBegin = bufStart;
        const char* textEnd = bufEnd;

        if (m_Filter.IsActive())
        {
            for (int lineIndex = 0; lineIndex < m_LineOffsets.Size; ++lineIndex)
            {
                const char* lineStart = bufStart + m_LineOffsets[lineIndex];
                const char* lineEnd = (lineIndex + 1 < m_LineOffsets.Size) ? (bufStart + m_LineOffsets[lineIndex + 1] - 1) : (bufEnd - 1);

                if (m_Filter.PassFilter(lineStart, lineEnd))
                {
                    filtered.append(lineStart, lineEnd);
                    filtered.push_back('\n');
                }
            }
            textBegin = filtered.c_str();
            textEnd = textBegin + filtered.size();
        }

        if (m_ScrollSelectableToBottomPending)
        {
            // x = -1 leaves horizontal scroll untouched; y = FLT_MAX gets clamped to the widget's
            // actual max scroll once its content size is known this frame - the standard ImGui
            // idiom for "scroll to bottom" via SetNextWindowScroll.
            ImGui::SetNextWindowScroll(ImVec2(-1.0f, FLT_MAX));
            m_ScrollSelectableToBottomPending = false;
        }

        ImGui::InputTextMultiline("##LogText", const_cast<char*>(textBegin), static_cast<size_t>(textEnd - textBegin) + 1, ImVec2(-FLT_MIN, -FLT_MIN), ImGuiInputTextFlags_ReadOnly | ImGuiInputTextFlags_WordWrap);
    }
    else
    {
        ImGui::BeginChild("LogScrollRegion", ImVec2(0, 0), false);

        // Word-wrapped lines have variable height, which rules out ImGuiListClipper's fixed-row-
        // height fast path, so this walks every line each frame rather than just the visible
        // slice. Acceptable at this volume; revisit with a height-aware clipper
        // (ImGuiListClipper::IncludeItemByIndex) if that changes.
        ImGui::PushTextWrapPos(0.0f);
        for (int lineIndex = 0; lineIndex < m_LineOffsets.Size; ++lineIndex)
        {
            const char* lineStart = bufStart + m_LineOffsets[lineIndex];
            const char* lineEnd = (lineIndex + 1 < m_LineOffsets.Size) ? (bufStart + m_LineOffsets[lineIndex + 1] - 1) : (bufEnd - 1);

            if (m_Filter.IsActive() && !m_Filter.PassFilter(lineStart, lineEnd))
                continue;

            ImGui::PushStyleColor(ImGuiCol_Text, LevelColor(static_cast<spdlog::level::level_enum>(m_LineLevels[lineIndex])));
            ImGui::TextUnformatted(lineStart, lineEnd);
            ImGui::PopStyleColor();
        }
        ImGui::PopTextWrapPos();

        if (m_AutoScroll && ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
            ImGui::SetScrollHereY(1.0f);

        ImGui::EndChild();
    }

    ImGui::End();
}
