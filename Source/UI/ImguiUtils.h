#pragma once

#include <imgui.h>

namespace ImGuiUtils
{
// Wraps ImGui::Text() to automatically wrap at the current column width, rather than
// overflowing the window and requiring a horizontal scroll bar. Returns true if the text
// was clicked (ImGui::IsItemClicked()).
inline void TextColorWrapped(const ImVec4& color, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextWrappedV(fmt, args);
    ImGui::PopStyleColor();
    va_end(args);
}

inline bool CheckboxTextWrapped(const char* id, bool* v, const char* fmt, ...)
{
    bool pressed = ImGui::Checkbox(id, v);

    ImGui::SameLine();

    va_list args;
    va_start(args, fmt);
    ImGui::TextWrappedV(fmt, args);
    va_end(args);

    return pressed;
}
}  // namespace ImGuiUtils