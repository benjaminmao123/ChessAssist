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
// (or pastes, via LoadFen) any position from scratch and studies it. Unlike SandboxSession, this
// class always has *a* position to show, starting from the standard starting position, and never
// resyncs to anything external. Keeps the *entire* played move list plus a cursor into it (not
// just "the end," like SandboxSession's undo-only history) so the user can step backward and
// forward through what they've played. Analyzed by its own dedicated EngineController/Stockfish
// process, same isolation reasoning as SandboxSession. Implements IPlayableBoard so the shared
// ChessBoardWidget mouse-interaction code can drive it the same way it drives SandboxSession.
class AnalysisBoardSession : public IPlayableBoard
{
public:
    explicit AnalysisBoardSession(EngineController& engine);

    // Discards all history and returns to ply 0 of the current starting point - the standard
    // starting position, or whatever was last loaded via LoadFen().
    void Reset();

    // Discards any custom FEN previously loaded via LoadFen() (and all history), returning the
    // starting point itself to the standard starting position - unlike Reset(), which returns to
    // ply 0 of whatever the *current* starting point is. The "start a fresh game" action, as
    // distinct from "back to move 0 of this position."
    void ResetToStandardStartingPosition();

    // Sets the position directly from a pasted FEN string (see FenWriter.h's ParseFen()),
    // discarding all history - it becomes the new ply-0 starting point (so Reset() returns to
    // it, not the standard position, until LoadFen() or a plain Reset() changes that again).
    // Returns false (state left unchanged) if fen is malformed.
    [[nodiscard]] bool LoadFen(std::string_view fen);

    // The position currently displayed (at GetCursor()) as FEN - the inverse of LoadFen(), for
    // the user to copy out and use elsewhere.
    [[nodiscard]] std::string GetFen() const;

    void FlipBoard();
    [[nodiscard]] bool IsFlipped() const;

    // Steps the cursor one ply toward the start/end of history and rebuilds the current
    // position - a no-op at either boundary, so always safe to call unconditionally.
    void StepBackward();
    void StepForward();
    [[nodiscard]] bool CanStepBackward() const;
    [[nodiscard]] bool CanStepForward() const;

    // 1-based "how far into history the displayed position is" / "how many plies exist" - e.g.
    // "Move 3 / 7". Counts plies (half-moves), not chess move numbers.
    [[nodiscard]] std::size_t GetCursor() const;
    [[nodiscard]] std::size_t HistoryLength() const;

    // IPlayableBoard
    [[nodiscard]] const BoardState& GetBoard() const override;
    [[nodiscard]] PieceColor GetSideToMove() const override;
    [[nodiscard]] std::optional<int> GetCheckedKingSquare() const override;
    [[nodiscard]] bool IsBlackAtBottom() const override;  // == IsFlipped()
    [[nodiscard]] std::vector<MoveGenerator::LegalMove> GetLegalMovesFrom(int from) const override;

    // Appends move, kicks off a fresh analysis search. If the cursor is behind the end of
    // history (the user stepped backward before playing this), the old "future" beyond the
    // cursor is discarded first - the standard PGN-viewer convention.
    void PlayMove(const MoveGenerator::LegalMove& move) override;

    // The analysis engine's suggestion for the current position - nullopt if no result has
    // arrived yet.
    [[nodiscard]] std::optional<std::string> GetSuggestedMove() const;

    // Whichever side the most recently issued analysis search is analyzing - safe to call from
    // any thread (see GameSession::GetRequestedSide()). Needed by EngineInfoPanel to flip UCI's
    // side-relative score into a consistent White-perspective display.
    [[nodiscard]] PieceColor GetRequestedSide() const;

    // Other candidate first moves beyond GetSuggestedMove()'s primary line - mirrors
    // SandboxSession::GetAlternateMoves(). Requires MultiPV > 1 on this session's engine (see
    // kMultiPvLines/App's ConfigureMultiPv()). Ordered by ascending multipv index.
    [[nodiscard]] std::vector<std::string> GetAlternateMoves() const;

    // The anticipated reply to GetSuggestedMove(), one ply beyond it - shown as a second,
    // lookahead arrow. Mirrors SandboxSession::GetLookaheadMove(): anchored against
    // GetSuggestedMove() and validated against a position with that suggestion actually applied
    // (a pawn move, especially en passant, can otherwise look outright illegal since the
    // displayed board is still one ply behind). Returns nullopt if no candidate has arrived yet,
    // it's stale, or it fails validation.
    [[nodiscard]] std::optional<std::string> GetLookaheadMove() const;

    // Routed from EngineController::SetOnBestMove for the analysis controller - called on its
    // reader thread, must not touch m_Current/m_History directly.
    void OnEngineBestMove(const BestMoveResult& result);

    // Routed from EngineController::SetOnInfo for the analysis controller - same threading
    // constraints as OnEngineBestMove. Collects multipv >= 2 lines' first moves for
    // GetAlternateMoves() and the multipv 1 line's PV[0]/PV[1] for GetLookaheadMove(), the same
    // way SandboxSession::OnEngineInfo does.
    void OnEngineInfo(const SearchInfo& info);

private:
    void RequestAnalysis();
    void RebuildCurrent();

    EngineController* m_Engine = nullptr;

    MoveGenerator::PositionState m_StartPosition;  // standard start, or the last LoadFen()'d position
    std::vector<MoveGenerator::LegalMove> m_History;
    std::size_t m_Cursor = 0;
    MoveGenerator::PositionState m_Current;  // m_StartPosition with m_History[0, m_Cursor) replayed

    bool m_Flipped = false;

    std::atomic<PieceColor> m_RequestedSide{PieceColor::White};

    mutable std::mutex m_SuggestedMoveMutex;
    std::optional<std::string> m_SuggestedMove;  // guarded by m_SuggestedMoveMutex

    // See MultiPvCollector's own comment - same shared type GameSession/SandboxSession use.
    // Internally thread-safe: written by OnEngineInfo (reader thread), cleared before every new
    // search (UI thread).
    MultiPvCollector m_AlternateMoves;

    // OwnMove/ReplyMove are PV[0]/PV[1] of the primary (multipv 1) line - "the current
    // suggestion is expected to be met with this reply." Written by OnEngineInfo (reader thread,
    // "last update wins" as the search deepens), cleared before every new search (UI thread) -
    // same pattern as SandboxSession's own.
    struct LookaheadCandidate
    {
        std::string OwnMove;
        std::string ReplyMove;
    };
    mutable std::mutex m_LookaheadMutex;
    std::optional<LookaheadCandidate> m_LookaheadCandidate;  // guarded by m_LookaheadMutex
};
