#pragma once

#include "../Vision/VisionTypes.h"

#include <optional>
#include <string>

namespace MoveDetector
{
// Diffs two board states and returns a candidate UCI move (e.g. "e2e4", "e7e8q"), or
// nullopt if the diff doesn't match a recognizable move shape.
std::optional<std::string> DetectMove(const BoardState& before, const BoardState& after, PieceColor sideToMove);
}  // namespace MoveDetector
