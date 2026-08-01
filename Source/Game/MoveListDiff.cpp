#include "MoveListDiff.h"

MoveListDiff ComputeMoveListDiff(std::size_t currentMoveCount, std::size_t alreadyAppliedCount)
{
    if (currentMoveCount == alreadyAppliedCount)
        return {MoveListDiffKind::NoChange};

    if (currentMoveCount < alreadyAppliedCount)
    {
        // Unambiguously "a fresh game just started" only when the list dropped to 0 or 1 -
        // anything else could be a real reset or a flaky/mid-render DOM read, so it's left
        // for the caller to treat as a desync rather than guessed at here.
        if (currentMoveCount <= 1)
            return {MoveListDiffKind::ResetToFreshGame};

        return {MoveListDiffKind::AmbiguousShrink};
    }

    return {MoveListDiffKind::Grew, alreadyAppliedCount};
}
