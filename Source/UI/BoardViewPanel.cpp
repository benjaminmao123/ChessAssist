#include "BoardViewPanel.h"

#include <imgui.h>

void BoardViewPanel::UpdateFrame(const cv::Mat& frame)
{
    m_Texture.Upload(frame);
}

void BoardViewPanel::Draw()
{
    ImGui::Begin("Board");

    if (m_Texture.IsValid())
        ImGui::Image(m_Texture.Id(), ImVec2(static_cast<float>(m_Texture.Width()), static_cast<float>(m_Texture.Height())));
    else
        ImGui::TextDisabled("No frame captured yet.");

    ImGui::End();
}
