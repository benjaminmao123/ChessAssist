#pragma once

#include <mutex>
#include <optional>

// Tracks the tracked player's average per-move accuracy - see GameSession::GetAccuracyPercent's
// full comment for the scoring scheme this implements: for each move, the engine's evaluation
// of the position right before it (the best achievable result) is compared against its
// evaluation right after it actually landed, and the drop between the two ("centipawn loss") is
// converted to a 0-100 score via the same exponential-decay curve chess.com's own accuracy
// metric uses, then averaged across the game. Extracted out of GameSession since it's a
// self-contained state machine (paired "before"/"after" evals -> a running average) independent
// of everything else that class does. Internally thread-safe: RecordBeforeEval/RecordAfterEval/
// TryScoreMove/ClearPendingEvals are meant to be called from the engine's background reader
// thread (see GameSession::OnEngineInfo/OnEngineBestMove), GetPercent/Reset from the UI thread.
class AccuracyTracker
{
public:
    // Both perspectiveCp values are in the tracked player's own perspective (positive = good
    // for them) - continuously overwritten ("last update wins", the search deepening) while
    // their respective side's search is in flight.
    void RecordBeforeEval(float perspectiveCp);
    void RecordAfterEval(float perspectiveCp);

    struct MoveScore
    {
        float MoveAccuracyPercent;
        float RunningAveragePercent;
        int MoveCount;
    };

    // If both a "before" and an "after" eval are currently pending, scores the move they
    // bracket (see the class comment) and folds it into the running average, returning the
    // result. Always clears both pending evals before returning, whether or not a score
    // resulted - a move only ever gets scored once. A move only gets scored if both a "before"
    // and an "after" search actually ran and completed - a Blitz/premove-skipped position just
    // isn't counted, rather than guessed at.
    [[nodiscard]] std::optional<MoveScore> TryScoreMove();

    // Discards any pending evals without scoring them - used when a move was played without a
    // fresh "before" search having run for it (e.g. a premove hit), so a stale eval left over
    // from an earlier, unrelated position doesn't get incorrectly paired with a later one.
    void ClearPendingEvals();

    [[nodiscard]] std::optional<float> GetPercent() const;

    // Clears all state, including the running average - called when accuracy should be scoped
    // to a fresh game rather than accumulating indefinitely across separate games.
    void Reset();

private:
    mutable std::mutex m_Mutex;
    std::optional<float> m_PendingBeforeMoveEvalCp;
    std::optional<float> m_LatestAfterMoveEvalCp;
    double m_SumPercent = 0.0;
    int m_MoveCount = 0;
};
