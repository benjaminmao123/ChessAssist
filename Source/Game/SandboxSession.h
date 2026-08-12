#pragma once

#include "Chess/MoveGenerator.h"
#include "Engine/EngineTypes.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class EngineController;

// Owns a purely local, hypothetical continuation played out on the tracked board - never
// touches the live site (no CdpClient calls, unlike GameSession::PlayMoveOnBoard's real
// automation). Analyzed by its own dedicated EngineController/Stockfish process so exploring a
// line never disrupts or competes with GameSession's own live-game analysis loop. Always
// mirrors the live tracked position while no hypothetical move has been played yet - callers
// resync it (SyncToLivePosition) whenever the live position changes, so BoardStatePanel can
// read board/orientation/check-square straight off this class regardless of whether a
// hypothetical line is active.
class SandboxSession
{
public:
    explicit SandboxSession(EngineController& sandboxEngine);

    // Reseeds from the live tracked position, discarding all hypothetical moves. Called once
    // per frame by App whenever GameSession::GetPositionGeneration() changes.
    void SyncToLivePosition(const MoveGenerator::PositionState& livePosition, bool blackAtBottom);

    // Discards all hypothetical moves and returns to mirroring the last-synced live position -
    // the manual "Reset" button's action.
    void ResetToLive();

    [[nodiscard]] bool IsActive() const;  // true once >= 1 hypothetical move has been played
    [[nodiscard]] std::size_t HistoryLength() const;

    [[nodiscard]] std::vector<MoveGenerator::LegalMove> GetLegalMovesFrom(int from) const;

    void PlayMove(const MoveGenerator::LegalMove& move);  // appends to history, kicks off a fresh sandbox search
    void UndoLastMove();                                   // pops the last hypothetical move and re-derives the position

    [[nodiscard]] const BoardState& GetBoard() const;
    [[nodiscard]] PieceColor GetSideToMove() const;
    [[nodiscard]] std::optional<int> GetCheckedKingSquare() const;
    [[nodiscard]] bool IsBlackAtBottom() const;

    // The sandbox engine's suggestion for the current hypothetical position - nullopt if
    // !IsActive() (nothing hypothetical to analyze) or no result has arrived yet.
    [[nodiscard]] std::optional<std::string> GetSuggestedMove() const;

    // Whichever side the most recently issued sandbox search is analyzing - safe to call from
    // any thread (see GameSession::GetRequestedSide(), the same pattern for the live engine).
    [[nodiscard]] PieceColor GetRequestedSide() const;

    // Routed from EngineController::SetOnBestMove for the sandbox controller - called on its
    // reader thread, must not touch m_Current/m_History/m_LiveSnapshot directly.
    void OnEngineBestMove(const BestMoveResult& result);

private:
    void RequestSandboxSearch();      // FEN-serializes m_Current, calls m_Engine->FindBestMoveAsync
    void RebuildCurrentAndRequery();  // replays m_LiveSnapshot + m_History into m_Current, then searches or stops

    EngineController* m_Engine = nullptr;

    MoveGenerator::PositionState m_LiveSnapshot;
    bool m_LiveBlackAtBottom = false;

    std::vector<MoveGenerator::LegalMove> m_History;
    MoveGenerator::PositionState m_Current;  // m_LiveSnapshot with m_History replayed onto it

    std::atomic<PieceColor> m_RequestedSide{PieceColor::White};

    mutable std::mutex m_SuggestedMoveMutex;
    std::optional<std::string> m_SuggestedMove;  // guarded by m_SuggestedMoveMutex
};
