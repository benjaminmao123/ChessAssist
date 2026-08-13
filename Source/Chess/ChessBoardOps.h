#pragma once

#include "ChessTypes.h"

#include <optional>

// Shared board-geometry primitives (pure functions over an explicit BoardState) so ChessRules
// and MoveGenerator can reuse the same attack-detection logic instead of duplicating it.
namespace ChessBoardOps
{
PieceColor Opposite(PieceColor color);

// True if every square strictly between from and to is empty. Caller must ensure from/to lie
// on a straight or diagonal line.
[[nodiscard]] bool SlidingPathClear(const BoardState& board, int from, int to);

// Pure geometric reachability for every piece type except Pawn (see PawnCanReach) - doesn't
// check whose turn it is or whether `to` is occupied by a friendly piece.
[[nodiscard]] bool CanPieceReach(const BoardState& board, PieceType type, int from, int to);

[[nodiscard]] bool PawnCanReach(const BoardState& board, int from, int to, PieceColor color, bool isCapture, std::optional<int> enPassantTarget);

// True if any piece of byColor attacks square (pure geometry - ignores pins, since a pinned
// piece still attacks the squares it threatens even though it can't legally move there).
[[nodiscard]] bool IsSquareAttacked(const BoardState& board, int square, PieceColor byColor);

[[nodiscard]] std::optional<int> FindKing(const BoardState& board, PieceColor color);

// Mutates board in place: moves the piece from `from` to `to` (promoting if `promotion` is
// set), removing the captured pawn at enPassantCaptureSquare if given. Doesn't validate
// legality.
void ApplyMoveOnBoard(BoardState& board, int from, int to, std::optional<PieceType> promotion, std::optional<int> enPassantCaptureSquare);

// Mutates board in place for a castling move: relocates king and rook, clearing both origin
// squares. Doesn't touch castling rights, en passant target, or side to move - callers own
// those (see ForfeitCastlingRightsForMove). Doesn't validate legality.
void ApplyCastleOnBoard(BoardState& board, int kingFrom, int kingTo, int rookFrom, int rookTo);

// Square the captured pawn sits on for an en passant capture: same file as the destination,
// same rank as the capturing pawn's origin.
[[nodiscard]] int EnPassantCaptureSquare(int from, int to);

// Has-moved castling-rights forfeiture for one applied move: a king move forfeits both rights
// for its side; either end of the move landing on a rook's home square (moved away, or just
// captured there) forfeits that right - checked unconditionally against `from`/`to` regardless
// of movedType.
void ForfeitCastlingRightsForMove(CastlingRights& rights, PieceType movedType, PieceColor movedColor, int from, int to);
}  // namespace ChessBoardOps
