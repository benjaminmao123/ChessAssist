#pragma once

#include "ChessTypes.h"

#include <optional>
#include <string>

// Serializes a position to Forsyth-Edwards Notation, for handing a hypothetical (sandbox)
// position to a UCI engine via the "position fen ..." command. Halfmove-clock and
// fullmove-number fields are always written as "0 1" - neither is tracked anywhere in this
// codebase (GameTracker's own base-FEN-plus-move-list design never needed them either), and a
// short-lived hypothetical sandbox line has no meaningful history to report them from.
[[nodiscard]] std::string ToFen(const BoardState& board, PieceColor sideToMove, CastlingRights rights, std::optional<int> enPassantTarget);
