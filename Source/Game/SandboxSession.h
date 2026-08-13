#pragma once

#include "IPlayableBoard.h"

#include "Chess/MoveGenerator.h"
#include "Engine/EngineTypes.h"
#include "Engine/MultiPvCollector.h"

#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class EngineController;

// Owns a purely local, hypothetical continuation played out on the tracked board - never
// touches the live site, and is analyzed by its own dedicated EngineController/Stockfish
// process so exploring a line never disrupts GameSession's own live-game analysis. Mirrors the
// live tracked position until a hypothetical move is played; callers resync it
// (SyncToLivePosition) whenever the live position changes. Implements IPlayableBoard so the
// shared ChessBoardWidget mouse-interaction code can drive it the same way it drives the
// unrelated AnalysisBoardSession.
class SandboxSession : public IPlayableBoard
{
public:
    explicit SandboxSession(EngineController& sandboxEngine);

    // Reseeds from the live tracked position, discarding all hypothetical moves. Called by App
    // whenever GameSession::GetPositionGeneration() changes.
    void SyncToLivePosition(const MoveGenerator::PositionState& livePosition, bool blackAtBottom);

    // Discards all hypothetical moves and returns to mirroring the last-synced live position -
    // the manual "Reset" button's action.
    void ResetToLive();

    [[nodiscard]] bool IsActive() const;  // true once >= 1 hypothetical move has been played
    [[nodiscard]] std::size_t HistoryLength() const;

    [[nodiscard]] std::vector<MoveGenerator::LegalMove> GetLegalMovesFrom(int from) const override;

    void PlayMove(const MoveGenerator::LegalMove& move) override;
    void UndoLastMove();

    [[nodiscard]] const BoardState& GetBoard() const override;
    [[nodiscard]] PieceColor GetSideToMove() const override;
    [[nodiscard]] std::optional<int> GetCheckedKingSquare() const override;
    [[nodiscard]] bool IsBlackAtBottom() const override;

    // The sandbox engine's suggestion for the current hypothetical position - nullopt if
    // !IsActive() or no result has arrived yet.
    [[nodiscard]] std::optional<std::string> GetSuggestedMove() const;

    // Whichever side the most recently issued sandbox search is analyzing - safe to call from
    // any thread (see GameSession::GetRequestedSide(), the same pattern for the live engine).
    [[nodiscard]] PieceColor GetRequestedSide() const;

    // Other candidate first moves beyond GetSuggestedMove()'s primary line - mirrors
    // GameSession::GetAlternateMoves(). Requires MultiPV > 1 on the sandbox engine, set once by
    // App right after starting it. Ordered by ascending multipv index.
    [[nodiscard]] std::vector<std::string> GetAlternateMoves() const;

    // The anticipated reply to GetSuggestedMove(), one ply beyond it - shown as a second,
    // lookahead arrow. Simpler than GameSession::GetLookaheadMove(): the sandbox has no "our
    // turn vs. opponent's turn" concept, so this is always just "the reply to the current
    // suggestion," anchored against GetSuggestedMove() and validated against a position with
    // that suggestion actually applied (a pawn move, especially en passant, can otherwise look
    // outright illegal since the displayed board is still one ply behind - see
    // GameSession::GetLookaheadMove()). Returns nullopt if !IsActive(), no candidate has arrived
    // yet, it's stale, or it fails validation.
    [[nodiscard]] std::optional<std::string> GetLookaheadMove() const;

    // Routed from EngineController::SetOnBestMove for the sandbox controller - called on its
    // reader thread, must not touch m_Current/m_History/m_LiveSnapshot directly.
    void OnEngineBestMove(const BestMoveResult& result);

    // Routed from EngineController::SetOnInfo for the sandbox controller - same threading
    // constraints as OnEngineBestMove. Collects multipv >= 2 lines' first moves for
    // GetAlternateMoves(), the same way GameSession::OnEngineInfo does for the live engine.
    void OnEngineInfo(const SearchInfo& info);

private:
    void RequestSandboxSearch();
    void RebuildCurrentAndRequery();

    EngineController* m_Engine = nullptr;

    MoveGenerator::PositionState m_LiveSnapshot;
    bool m_LiveBlackAtBottom = false;

    std::vector<MoveGenerator::LegalMove> m_History;
    MoveGenerator::PositionState m_Current;  // m_LiveSnapshot with m_History replayed onto it

    std::atomic<PieceColor> m_RequestedSide{PieceColor::White};

    mutable std::mutex m_SuggestedMoveMutex;
    std::optional<std::string> m_SuggestedMove;  // guarded by m_SuggestedMoveMutex

    // See MultiPvCollector's own comment - same shared type GameSession uses. Internally
    // thread-safe: written by OnEngineInfo (reader thread), cleared before every new search (UI
    // thread).
    MultiPvCollector m_AlternateMoves;

    // OwnMove/ReplyMove are PV[0]/PV[1] of the primary (multipv 1) line - "the current
    // suggestion is expected to be met with this reply." Written by OnEngineInfo (reader
    // thread, "last update wins" as the search deepens), cleared before every new search (UI
    // thread).
    struct LookaheadCandidate
    {
        std::string OwnMove;
        std::string ReplyMove;
    };
    mutable std::mutex m_LookaheadMutex;
    std::optional<LookaheadCandidate> m_LookaheadCandidate;  // guarded by m_LookaheadMutex
};
