#pragma once

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

    // Other candidate first moves for the current hypothetical position, beyond
    // GetSuggestedMove()'s primary line - mirrors GameSession::GetAlternateMoves(). Requires
    // MultiPV > 1 on the sandbox engine, set once by App right after starting it (no "restart"
    // button exists for this engine, unlike the live one, so no reapplication-on-restart
    // concern). Ordered by ascending multipv index, like GameSession::GetAlternateMoves().
    [[nodiscard]] std::vector<std::string> GetAlternateMoves() const;

    // The anticipated reply to GetSuggestedMove() - one ply beyond it, from the same search's
    // PV[0]/PV[1] - meant to be shown as a second, lookahead arrow alongside the primary one.
    // Mirrors GameSession::GetLookaheadMove(), but simpler: the sandbox has no "our turn vs.
    // opponent's turn" concept (either side's pieces can be dragged), so this is always just
    // "the reply to the current suggestion," anchored against GetSuggestedMove() and validated
    // against a position with that suggestion actually applied first (same reasoning as
    // GameSession::GetLookaheadMove()'s comment - a pawn move, especially en passant, can look
    // outright illegal otherwise, since the displayed board is still one ply behind). Returns
    // nullopt if !IsActive(), no candidate has arrived yet, it's stale relative to the current
    // suggestion, or it fails that validation.
    [[nodiscard]] std::optional<std::string> GetLookaheadMove() const;

    // Routed from EngineController::SetOnBestMove for the sandbox controller - called on its
    // reader thread, must not touch m_Current/m_History/m_LiveSnapshot directly.
    void OnEngineBestMove(const BestMoveResult& result);

    // Routed from EngineController::SetOnInfo for the sandbox controller - called on its reader
    // thread, same constraints as OnEngineBestMove. Collects multipv >= 2 lines' first moves
    // for GetAlternateMoves(), the same way GameSession::OnEngineInfo does for the live engine.
    void OnEngineInfo(const SearchInfo& info);

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

    // See MultiPvCollector's own comment - same shared type GameSession uses for the identical
    // purpose. Internally thread-safe: written by OnEngineInfo (reader thread), cleared before
    // every new search (UI thread).
    MultiPvCollector m_AlternateMoves;

    // OwnMove/ReplyMove are PV[0]/PV[1] of the primary (multipv 1) line - "the current
    // suggestion is expected to be met with this reply." Written by OnEngineInfo (reader
    // thread, "last update wins" as the search deepens) and cleared before every new search (UI
    // thread) - same pattern as m_SuggestedMove/m_AlternateMoves.
    struct LookaheadCandidate
    {
        std::string OwnMove;
        std::string ReplyMove;
    };
    mutable std::mutex m_LookaheadMutex;
    std::optional<LookaheadCandidate> m_LookaheadCandidate;  // guarded by m_LookaheadMutex
};
