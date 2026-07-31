#include "EngineInfoPanel.h"

#include <imgui.h>

#include <string>

void EngineInfoPanel::UpdateInfo(const SearchInfo& info)
{
    std::scoped_lock lock(m_Mutex);
    m_LatestInfo = info;
}

void EngineInfoPanel::UpdateBestMove(const BestMoveResult& result)
{
    std::scoped_lock lock(m_Mutex);
    m_LatestBestMove = result;
}

void EngineInfoPanel::Draw()
{
    std::optional<SearchInfo> info;
    std::optional<BestMoveResult> bestMove;
    {
        std::scoped_lock lock(m_Mutex);
        info = m_LatestInfo;
        bestMove = m_LatestBestMove;
    }

    ImGui::Begin("Engine");

    if (info)
    {
        ImGui::Text("Depth: %d", info->Depth);

        if (info->ScoreMate)
            ImGui::Text("Score: mate in %d", *info->ScoreMate);
        else if (info->ScoreCp)
            ImGui::Text("Score: %.2f", static_cast<double>(*info->ScoreCp) / 100.0);
        else
            ImGui::TextDisabled("Score: -");

        ImGui::Text("Nodes: %s", std::to_string(info->Nodes.value_or(0)).c_str());
        ImGui::Text("Nps: %s", std::to_string(info->Nps.value_or(0)).c_str());

        if (!info->Pv.empty())
        {
            std::string pv;
            for (const std::string& move : info->Pv)
            {
                if (!pv.empty())
                    pv += ' ';
                pv += move;
            }
            ImGui::TextWrapped("PV: %s", pv.c_str());
        }
    }
    else
    {
        ImGui::TextDisabled("No search info yet.");
    }

    ImGui::Separator();

    if (bestMove)
        ImGui::Text("Best move: %s", bestMove->BestMove.c_str());
    else
        ImGui::TextDisabled("Best move: -");

    ImGui::End();
}
