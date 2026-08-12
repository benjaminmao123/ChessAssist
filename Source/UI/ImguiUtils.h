#pragma once

#include <imgui.h>

namespace ImGuiUtils
{
// Wraps ImGui::Text() to automatically wrap at the current column width, rather than
// overflowing the window and requiring a horizontal scroll bar. Returns true if the text
// was clicked (ImGui::IsItemClicked()).
/*
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.4f, 0.4f, 1.0f));
        ImGui::TextWrapped("Tracking lost sync - click Connect to resync");
        ImGui::PopStyleColor();
*/
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
    // 1. Render the checkbox with an invisible label using the provided ID
    //    This prevents the checkbox from rendering its own text.
    bool pressed = ImGui::Checkbox(id, v);

    // 2. Align the text horizontally on the same line as the checkbox
    ImGui::SameLine();

    // 3. Handle standard printf-style formatting safely
    va_list args;
    va_start(args, fmt);
    ImGui::TextWrappedV(fmt, args);
    va_end(args);

    return pressed;
}
}  // namespace ImGuiUtils