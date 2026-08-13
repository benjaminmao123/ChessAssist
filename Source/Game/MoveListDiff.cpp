#include "MoveListDiff.h"

MoveListDiff ComputeMoveListDiff(std::size_t currentMoveCount, std::size_t alreadyAppliedCount)
{
    if (currentMoveCount == alreadyAppliedCount)
        return {MoveListDiffKind::NoChange};

    if (currentMoveCount < alreadyAppliedCount)
    {
        // Only a drop to 0 or 1 is unambiguously "a fresh game started" - anything else could
        // be a real desync or a flaky/mid-render DOM read, so it's left for the caller to decide.
        if (currentMoveCount <= 1)
            return {MoveListDiffKind::ResetToFreshGame};

        return {MoveListDiffKind::AmbiguousShrink};
    }

    return {MoveListDiffKind::Grew, alreadyAppliedCount};
}
