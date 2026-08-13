#pragma once

#include <cstddef>

enum class MoveListDiffKind
{
    NoChange,
    Grew,
    ResetToFreshGame,
    AmbiguousShrink,
};

struct MoveListDiff
{
    MoveListDiffKind Kind;
    std::size_t StartIndex = 0;  // first new-move index into the current list; only meaningful when Kind == Grew
};

// Pure decision logic for GameSession::Poll()'s SAN-move-list diffing, split out for
// testability (no CdpClient/ChessRules/browser dependency). currentMoveCount is the site's
// current move-list length; alreadyAppliedCount is how many moves GameSession has already
// applied to ChessRules/GameTracker.
[[nodiscard]] MoveListDiff ComputeMoveListDiff(std::size_t currentMoveCount, std::size_t alreadyAppliedCount);
