#include "AccuracyTracker.h"

#include <algorithm>
#include <cmath>

namespace
{
// The same exponential-decay curve chess.com's own accuracy metric is built on: centipawn
// loss -> a 0-100 per-move score. Clamped since the raw formula can slightly exceed 100 at
// (near-)zero loss and go negative for very large losses.
float ScoreLossToAccuracyPercent(float centipawnLoss)
{
    const float accuracy = 103.1668f * std::exp(-0.04354f * centipawnLoss) - 3.1669f;
    return std::clamp(accuracy, 0.0f, 100.0f);
}
}  // namespace

void AccuracyTracker::RecordBeforeEval(float perspectiveCp)
{
    std::scoped_lock lock(m_Mutex);
    m_PendingBeforeMoveEvalCp = perspectiveCp;
}

void AccuracyTracker::RecordAfterEval(float perspectiveCp)
{
    std::scoped_lock lock(m_Mutex);
    m_LatestAfterMoveEvalCp = perspectiveCp;
}

std::optional<AccuracyTracker::MoveScore> AccuracyTracker::TryScoreMove()
{
    std::scoped_lock lock(m_Mutex);

    std::optional<MoveScore> result;
    if (m_PendingBeforeMoveEvalCp && m_LatestAfterMoveEvalCp)
    {
        const float lossCp = std::max(0.0f, *m_PendingBeforeMoveEvalCp - *m_LatestAfterMoveEvalCp);
        const float moveAccuracy = ScoreLossToAccuracyPercent(lossCp);
        m_SumPercent += moveAccuracy;
        ++m_MoveCount;
        result = MoveScore{moveAccuracy, static_cast<float>(m_SumPercent / m_MoveCount), m_MoveCount};
    }

    m_PendingBeforeMoveEvalCp.reset();
    m_LatestAfterMoveEvalCp.reset();

    return result;
}

void AccuracyTracker::ClearPendingEvals()
{
    std::scoped_lock lock(m_Mutex);
    m_PendingBeforeMoveEvalCp.reset();
    m_LatestAfterMoveEvalCp.reset();
}

std::optional<float> AccuracyTracker::GetPercent() const
{
    std::scoped_lock lock(m_Mutex);
    if (m_MoveCount == 0)
        return std::nullopt;

    return static_cast<float>(m_SumPercent / m_MoveCount);
}

void AccuracyTracker::Reset()
{
    std::scoped_lock lock(m_Mutex);
    m_PendingBeforeMoveEvalCp.reset();
    m_LatestAfterMoveEvalCp.reset();
    m_SumPercent = 0.0;
    m_MoveCount = 0;
}
