#pragma once

#include "IPlayableBoard.h"

#include "Chess/MoveGenerator.h"
#include "Engine/EngineTypes.h"
#include "Engine/MultiPvCollector.h"

#include <atomic>
#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class EngineController;

// A free-standing position-analysis tool, entirely independent of the live tracked game and of
// SandboxSession's hypothetical-line-from-the-live-position exploration - the user plays out
// (or pastes, via LoadFen) any position from scratch and studies it. Unlike SandboxSession,
// which only activates once >=1 hypothetical move exists and always mirrors the live position
// when inactive, this class always has *a* position to show and analyze, starting from the
// standard chess starting position, and never resyncs itself to anything external. Keeps the
// *entire* played move list plus a cursor into it (not just "the end," like SandboxSession's
// undo-only history) so the user can step backward and forward through what they've played.
// Analyzed by its own dedicated EngineController/Stockfish process, same isolation reasoning as
// SandboxSession's own dedicated engine. Implements IPlayableBoard so the shared
// ChessBoardWidget mouse-interaction code (see Source/UI/ChessBoardWidget.h) can drive it the
// same way it drives the unrelated SandboxSession.
class AnalysisBoardSession : public IPlayableBoard
{
public:
    explicit AnalysisBoardSession(EngineController& engine);

    // Discards all history and returns to ply 0 of the current starting point - the standard
    // starting position, or whatever was last loaded via LoadFen().
    void Reset();

    // Discards any custom FEN previously loaded via LoadFen() (and all history), returning the
    // starting point itself to the standard chess starting position - unlike Reset(), which
    // returns to ply 0 of whatever the *current* starting point is without changing what that
    // is. The "start a fresh game" action, as distinct from "back to move 0 of this position."
    void ResetToStandardStartingPosition();

    // Sets the position directly from a pasted FEN string (see FenWriter.h's ParseFen()),
    // discarding all history - there's no earlier position for the loaded one to be a move away
    // from, it just becomes the new ply-0 starting point (so Reset() returns to it, not to the
    // standard starting position, until LoadFen() or Reset() with no prior LoadFen() changes
    // that again). Returns false (state left entirely unchanged) if fen is malformed.
    [[nodiscard]] bool LoadFen(std::string_view fen);

    // The position currently displayed (i.e. at GetCursor(), not necessarily the starting
    // point) as FEN (see FenWriter.h's ToFen()) - the inverse of LoadFen(), for the user to
    // copy out and use elsewhere.
    [[nodiscard]] std::string GetFen() const;

    void FlipBoard();
    [[nodiscard]] bool IsFlipped() const;

    // Steps the cursor one ply toward the start/end of history and rebuilds the current
    // position from it - a no-op (not clamped-and-reapplied) at either boundary, so these are
    // always safe to call unconditionally from e.g. an unguarded hotkey handler.
    void StepBackward();
    void StepForward();
    [[nodiscard]] bool CanStepBackward() const;
    [[nodiscard]] bool CanStepForward() const;

    // 1-based "how far into history the displayed position is" / "how many plies exist" - e.g.
    // "Move 3 / 7" - not to be confused with a chess move NUMBER (a full move pair); this counts
    // plies (half-moves), matching how PlayMove()/StepBackward()/StepForward() themselves work.
    [[nodiscard]] std::size_t GetCursor() const;
    [[nodiscard]] std::size_t HistoryLength() const;

    // IPlayableBoard
    [[nodiscard]] const BoardState& GetBoard() const override;
    [[nodiscard]] PieceColor GetSideToMove() const override;
    [[nodiscard]] std::optional<int> GetCheckedKingSquare() const override;
    [[nodiscard]] bool IsBlackAtBottom() const override;  // == IsFlipped()
    [[nodiscard]] std::vector<MoveGenerator::LegalMove> GetLegalMovesFrom(int from) const override;

    // Appends move, kicks off a fresh analysis search. If the cursor is currently behind the
    // end of history (the user stepped backward before playing this), the old "future" beyond
    // the cursor is discarded first - the standard PGN-viewer convention: deviating from a
    // previously explored line replaces it rather than branching.
    void PlayMove(const MoveGenerator::LegalMove& move) override;

    // The analysis engine's suggestion for the current position - nullopt if no result has
    // arrived yet for the position currently displayed.
    [[nodiscard]] std::optional<std::string> GetSuggestedMove() const;

    // Whichever side the most recently issued analysis search is analyzing - safe to call from
    // any thread (see GameSession::GetRequestedSide(), the same pattern for the live engine) -
    // needed by EngineInfoPanel to flip UCI's side-to-move-relative score into a consistent
    // White-perspective display.
    [[nodiscard]] PieceColor GetRequestedSide() const;

    // Other candidate first moves for the current position, beyond GetSuggestedMove()'s primary
    // line - mirrors SandboxSession::GetAlternateMoves(). Requires MultiPV > 1 on this session's
    // engine (see kMultiPvLines/App's ConfigureMultiPv() call for it). Ordered by ascending
    // multipv index, like GameSession::GetAlternateMoves().
    [[nodiscard]] std::vector<std::string> GetAlternateMoves() const;

    // The anticipated reply to GetSuggestedMove() - one ply beyond it, from the same search's
    // PV[0]/PV[1] - shown as a second, lookahead arrow alongside the primary one. Mirrors
    // SandboxSession::GetLookaheadMove(): anchored against GetSuggestedMove() and validated
    // against a position with that suggestion actually applied first (a pawn move, especially en
    // passant, can otherwise look outright illegal, since the displayed board is still one ply
    // behind). Returns nullopt if no candidate has arrived yet, it's stale relative to the
    // current suggestion, or it fails that validation.
    [[nodiscard]] std::optional<std::string> GetLookaheadMove() const;

    // Routed from EngineController::SetOnBestMove for the analysis controller - called on its
    // reader thread, must not touch m_Current/m_History directly.
    void OnEngineBestMove(const BestMoveResult& result);

    // Routed from EngineController::SetOnInfo for the analysis controller - called on its reader
    // thread, same constraints as OnEngineBestMove. Collects multipv >= 2 lines' first moves for
    // GetAlternateMoves() and the multipv 1 line's PV[0]/PV[1] for GetLookaheadMove(), the same
    // way SandboxSession::OnEngineInfo does.
    void OnEngineInfo(const SearchInfo& info);

private:
    void RequestAnalysis();  // FEN-serializes m_Current, calls m_Engine->FindBestMoveAsync
    void RebuildCurrent();   // replays m_StartPosition + m_History[0, m_Cursor) into m_Current

    EngineController* m_Engine = nullptr;

    MoveGenerator::PositionState m_StartPosition;  // standard start, or the last LoadFen()'d position
    std::vector<MoveGenerator::LegalMove> m_History;
    std::size_t m_Cursor = 0;
    MoveGenerator::PositionState m_Current;  // m_StartPosition with m_History[0, m_Cursor) replayed

    bool m_Flipped = false;

    std::atomic<PieceColor> m_RequestedSide{PieceColor::White};

    mutable std::mutex m_SuggestedMoveMutex;
    std::optional<std::string> m_SuggestedMove;  // guarded by m_SuggestedMoveMutex

    // See MultiPvCollector's own comment - same shared type GameSession/SandboxSession use for
    // the identical purpose. Internally thread-safe: written by OnEngineInfo (reader thread),
    // cleared before every new search (UI thread).
    MultiPvCollector m_AlternateMoves;

    // OwnMove/ReplyMove are PV[0]/PV[1] of the primary (multipv 1) line - "the current
    // suggestion is expected to be met with this reply." Written by OnEngineInfo (reader thread,
    // "last update wins" as the search deepens) and cleared before every new search (UI thread) -
    // same pattern as m_SuggestedMove/m_AlternateMoves, and as SandboxSession's own.
    struct LookaheadCandidate
    {
        std::string OwnMove;
        std::string ReplyMove;
    };
    mutable std::mutex m_LookaheadMutex;
    std::optional<LookaheadCandidate> m_LookaheadCandidate;  // guarded by m_LookaheadMutex
};
