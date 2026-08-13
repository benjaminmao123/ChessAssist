#pragma once

#include "Chess/ChessTypes.h"
#include "Engine/EngineTypes.h"

#include <mutex>
#include <optional>

// Bridges EngineController's background reader thread to the UI thread. ImGui is not
// thread-safe, so UpdateInfo/UpdateBestMove (called from the engine's reader thread) only
// stash a copy behind a mutex - Draw (called once per frame from the UI thread) reads it.
class EngineInfoPanel
{
public:
    // requestedSide is whichever side the search producing info is analyzing for - UCI
    // "score" is always relative to that side, not to White, so Draw() needs it to flip the
    // sign into the conventional "positive = good for White" display when it's Black's turn.
    void UpdateInfo(const SearchInfo& info, PieceColor requestedSide);
    void UpdateBestMove(const BestMoveResult& result);

    // Draws depth/score/nodes/nps/PV/best-move as plain widgets, without an owning
    // ImGui::Begin()/End() of its own - the caller is responsible for that. Not thread-safe:
    // call once per frame from the UI thread only, inside an existing window.
    void DrawContents();

    // White's-perspective evaluation fraction for an eval bar: 0 = totally winning for Black,
    // 1 = totally winning for White, 0.5 = equal. nullopt if no search info yet. A forced mate
    // always returns 0 or 1 outright; otherwise see the .cpp comment on the tanh compression.
    [[nodiscard]] std::optional<float> GetWhiteWinFraction() const;

    // Forced-mate read on the latest search info, if any (a plain cp score returns nullopt here).
    // DistanceInMoves is always positive (moves until mate); WhiteIsMating says which side is
    // actually delivering it, resolved the same mate-0-safe way as DrawContents() (see
    // SideToMoveMateMagnitude's comment).
    struct MateInfo
    {
        int DistanceInMoves = 0;
        bool WhiteIsMating = false;
    };
    [[nodiscard]] std::optional<MateInfo> GetMateInfo() const;

private:
    mutable std::mutex m_Mutex;
    std::optional<SearchInfo> m_LatestInfo;
    PieceColor m_LatestInfoSide = PieceColor::White;
    std::optional<BestMoveResult> m_LatestBestMove;
};
