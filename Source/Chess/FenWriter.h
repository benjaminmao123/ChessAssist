#pragma once

#include "ChessTypes.h"
#include "MoveGenerator.h"

#include <optional>
#include <string>
#include <string_view>

// Serializes a position to Forsyth-Edwards Notation, for handing a hypothetical (sandbox)
// position to a UCI engine via "position fen ...". Halfmove-clock/fullmove-number fields are
// always written as "0 1" since neither is tracked anywhere in this codebase.
[[nodiscard]] std::string ToFen(const BoardState& board, PieceColor sideToMove, CastlingRights rights, std::optional<int> enPassantTarget);

// Parses a FEN string into a full position, the inverse of ToFen() - used to seed
// AnalysisBoardSession from a user-pasted FEN. Returns nullopt for any malformed FEN: fewer
// than the 4 required fields (piece placement/side to move/castling/en passant - trailing
// halfmove-clock/fullmove-number are accepted but ignored), a piece-placement field not
// resolving to 8x8, an invalid character, or a side without exactly one king. Never partially
// applies a malformed FEN.
[[nodiscard]] std::optional<MoveGenerator::PositionState> ParseFen(std::string_view fen);
