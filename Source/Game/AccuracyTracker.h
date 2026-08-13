#pragma once

#include <mutex>
#include <optional>

// Tracks the tracked player's average per-move accuracy - see GameSession::GetAccuracyPercent's
// comment for the full scoring scheme (centipawn loss between the "before" and "after" eval of
// each move, converted to a 0-100 score and averaged). Internally thread-safe:
// RecordBeforeEval/RecordAfterEval/TryScoreMove/ClearPendingEvals are called from the engine's
// background reader thread (see GameSession::OnEngineInfo/OnEngineBestMove), GetPercent/Reset
// from the UI thread.
class AccuracyTracker
{
public:
    // perspectiveCp is in the tracked player's own perspective (positive = good for them) -
    // continuously overwritten ("last update wins") while the search is in flight.
    void RecordBeforeEval(float perspectiveCp);
    void RecordAfterEval(float perspectiveCp);

    struct MoveScore
    {
        float MoveAccuracyPercent;
        float RunningAveragePercent;
        int MoveCount;
    };

    // If both a "before" and an "after" eval are pending, scores the move they bracket and folds
    // it into the running average. Always clears both pending evals before returning, so a move
    // only ever gets scored once; a Blitz/premove-skipped position with no "after" search just
    // isn't counted.
    [[nodiscard]] std::optional<MoveScore> TryScoreMove();

    // Discards any pending evals without scoring them - used when a move was played without a
    // fresh "before" search (e.g. a premove hit), so a stale eval doesn't get paired with a
    // later, unrelated one.
    void ClearPendingEvals();

    [[nodiscard]] std::optional<float> GetPercent() const;

    // Clears all state, including the running average - used when starting a fresh game.
    void Reset();

private:
    mutable std::mutex m_Mutex;
    std::optional<float> m_PendingBeforeMoveEvalCp;
    std::optional<float> m_LatestAfterMoveEvalCp;
    double m_SumPercent = 0.0;
    int m_MoveCount = 0;
};
