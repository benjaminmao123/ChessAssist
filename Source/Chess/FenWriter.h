#pragma once

#include "ChessTypes.h"
#include "MoveGenerator.h"

#include <optional>
#include <string>
#include <string_view>

// Serializes a position to Forsyth-Edwards Notation, for handing a hypothetical (sandbox)
// position to a UCI engine via the "position fen ..." command. Halfmove-clock and
// fullmove-number fields are always written as "0 1" - neither is tracked anywhere in this
// codebase (GameTracker's own base-FEN-plus-move-list design never needed them either), and a
// short-lived hypothetical sandbox line has no meaningful history to report them from.
[[nodiscard]] std::string ToFen(const BoardState& board, PieceColor sideToMove, CastlingRights rights, std::optional<int> enPassantTarget);

// Parses a FEN string into a full position, the inverse of ToFen() - used to seed
// AnalysisBoardSession from a user-pasted FEN. Returns nullopt for any malformed FEN: wrong
// field count (fewer than the 4 required - piece placement/side to move/castling/en passant;
// the trailing halfmove-clock/fullmove-number fields are accepted but ignored if present, not
// tracked anywhere in this codebase - see ToFen()'s own comment), a piece-placement field that
// doesn't resolve to exactly 8 ranks of exactly 8 files each, an invalid piece/side-to-move/
// castling/en-passant-square character, or a side with zero or more than one king (a
// syntactically valid but unanalyzable position). Never partially applies a malformed FEN -
// either the whole thing parses or nothing does.
[[nodiscard]] std::optional<MoveGenerator::PositionState> ParseFen(std::string_view fen);
