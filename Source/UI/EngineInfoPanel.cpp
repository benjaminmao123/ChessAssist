#include "EngineInfoPanel.h"

#include <imgui.h>

#include <cmath>
#include <string>

namespace
{
// UCI "score" is relative to the side to move, not to White - flip the sign so it always
// follows the conventional "positive = good for White" reading, regardless of whose turn the
// engine was analyzing. Shared by DrawContents() (text) and GetWhiteWinFraction() (eval bar).
int WhitePerspectiveSign(PieceColor requestedSide)
{
    return (requestedSide == PieceColor::Black) ? -1 : 1;
}

// A signed, side-to-move-perspective magnitude for a mate score, suitable for multiplying by
// WhitePerspectiveSign() to get a White-perspective value. "mate 0" means the side to move is
// already checkmated - the worst outcome for them, not the best - so multiplying the *raw* mate
// value by sign would lose all sign information at exactly 0, making "White checkmated" and
// "Black checkmated" indistinguishable. Offsetting by kMateEquivalentCp avoids that.
float SideToMoveMateMagnitude(int mate)
{
    constexpr float kMateEquivalentCp = 10000.0f;
    return mate > 0 ? (kMateEquivalentCp - static_cast<float>(mate)) : (-kMateEquivalentCp - static_cast<float>(mate));
}
}  // namespace

void EngineInfoPanel::UpdateInfo(const SearchInfo& info, PieceColor requestedSide)
{
    // With MultiPV > 1 (see GameSession::kMultiPvLines), Stockfish emits one info line per
    // requested line per depth (multipv 1, 2, 3, ...) - without this filter, the depth/score/PV
    // text here would flicker to whichever (possibly weaker) line arrived most recently instead
    // of always tracking the engine's best line. Alternate lines are read directly off SearchInfo
    // elsewhere (see GameSession::GetAlternateMoves()), not through this display-only class.
    if (info.MultiPvIndex != 1)
        return;

    std::scoped_lock lock(m_Mutex);
    m_LatestInfo = info;
    m_LatestInfoSide = requestedSide;
}

void EngineInfoPanel::UpdateBestMove(const BestMoveResult& result)
{
    std::scoped_lock lock(m_Mutex);
    m_LatestBestMove = result;
}

void EngineInfoPanel::DrawContents()
{
    std::optional<SearchInfo> info;
    PieceColor infoSide = PieceColor::White;
    std::optional<BestMoveResult> bestMove;
    {
        std::scoped_lock lock(m_Mutex);
        info = m_LatestInfo;
        infoSide = m_LatestInfoSide;
        bestMove = m_LatestBestMove;
    }

    if (info)
    {
        ImGui::Text("Depth: %d", info->Depth);

        const int sign = WhitePerspectiveSign(infoSide);

        if (info->ScoreMate)
        {
            // A raw signed "mate in -3" reads like an error (a move count can't be negative), so
            // name the mating side explicitly instead of leaning on the sign convention. Sign is
            // resolved via SideToMoveMateMagnitude, not the raw mate value - see its comment on
            // why mate 0 needs that.
            const int mateDistance = std::abs(*info->ScoreMate);
            if (static_cast<float>(sign) * SideToMoveMateMagnitude(*info->ScoreMate) >= 0.0f)
                ImGui::Text("Score: White mates in %d", mateDistance);
            else
                ImGui::Text("Score: Black mates in %d", mateDistance);
        }
        else if (info->ScoreCp)
            ImGui::Text("Score: %.2f", sign * static_cast<double>(*info->ScoreCp) / 100.0);
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
}

std::optional<float> EngineInfoPanel::GetWhiteWinFraction() const
{
    std::optional<SearchInfo> info;
    PieceColor infoSide = PieceColor::White;
    {
        std::scoped_lock lock(m_Mutex);
        info = m_LatestInfo;
        infoSide = m_LatestInfoSide;
    }

    if (!info)
        return std::nullopt;

    const int sign = WhitePerspectiveSign(infoSide);

    if (info->ScoreMate)
        return (static_cast<float>(sign) * SideToMoveMateMagnitude(*info->ScoreMate) >= 0.0f) ? 1.0f : 0.0f;

    if (info->ScoreCp)
    {
        // Compresses large advantages via tanh rather than a hard linear clamp, so the bar
        // approaches (but never quite reaches) full only in truly lopsided positions - matches
        // the feel of lichess/chess.com-style eval bars better than a flat +-N cp cutoff.
        const float cpForWhite = static_cast<float>(sign * (*info->ScoreCp));
        return 0.5f + 0.5f * std::tanh(cpForWhite / 400.0f);
    }

    return std::nullopt;
}

std::optional<EngineInfoPanel::MateInfo> EngineInfoPanel::GetMateInfo() const
{
    std::optional<SearchInfo> info;
    PieceColor infoSide = PieceColor::White;
    {
        std::scoped_lock lock(m_Mutex);
        info = m_LatestInfo;
        infoSide = m_LatestInfoSide;
    }

    if (!info || !info->ScoreMate)
        return std::nullopt;

    const int sign = WhitePerspectiveSign(infoSide);
    const bool whiteIsMating = static_cast<float>(sign) * SideToMoveMateMagnitude(*info->ScoreMate) >= 0.0f;
    return MateInfo{std::abs(*info->ScoreMate), whiteIsMating};
}
