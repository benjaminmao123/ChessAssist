#include "CalibrationPanel.h"

#include <imgui.h>

#include <opencv2/imgproc.hpp>

void CalibrationPanel::Begin(const cv::Mat& frame, BoardOrientation orientation)
{
    m_Texture.Upload(frame);
    m_Orientation = orientation;
    m_Clicks.clear();
    m_Result.reset();
    m_Active = true;
}

bool CalibrationPanel::IsActive() const
{
    return m_Active;
}

void CalibrationPanel::Draw()
{
    if (!m_Active)
        return;

    ImGui::Begin("Calibrate Board");
    ImGui::TextWrapped("Click the board's top-left corner, then its bottom-right corner.");

    if (m_Texture.IsValid())
    {
        const ImVec2 imagePos = ImGui::GetCursorScreenPos();
        ImGui::Image(m_Texture.Id(), ImVec2(static_cast<float>(m_Texture.Width()), static_cast<float>(m_Texture.Height())));

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
        {
            const ImVec2 mousePos = ImGui::GetIO().MousePos;
            m_Clicks.emplace_back(static_cast<int>(mousePos.x - imagePos.x), static_cast<int>(mousePos.y - imagePos.y));
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        for (const cv::Point& click : m_Clicks)
        {
            const ImVec2 screenPoint(imagePos.x + static_cast<float>(click.x), imagePos.y + static_cast<float>(click.y));
            drawList->AddCircleFilled(screenPoint, 5.0f, IM_COL32(255, 0, 0, 255));
        }
    }
    else
    {
        ImGui::TextDisabled("No frame to calibrate against.");
    }

    const bool cancelled = ImGui::Button("Cancel") || ImGui::IsKeyPressed(ImGuiKey_Escape);

    ImGui::End();

    if (cancelled)
    {
        m_Active = false;
        m_Clicks.clear();
        return;
    }

    if (m_Clicks.size() >= 2)
    {
        m_Result = BoardRegion{cv::boundingRect(m_Clicks), m_Orientation};
        m_Active = false;
        m_Clicks.clear();
    }
}

std::optional<BoardRegion> CalibrationPanel::TakeResult()
{
    const std::optional<BoardRegion> result = m_Result;
    m_Result.reset();
    return result;
}
