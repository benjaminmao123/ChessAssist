#pragma once

#include "ChessTypes.h"

#include <optional>

// Shared board-geometry primitives - pure functions operating on an explicit BoardState, no
// class state of their own. Extracted out of ChessRules.cpp (which uses these internally to
// interpret SAN tokens) so MoveGenerator can reuse the exact same attack-detection logic for
// full legal-move enumeration instead of maintaining a second, drift-prone copy.
namespace ChessBoardOps
{
PieceColor Opposite(PieceColor color);

// True if every square strictly between from and to (a straight or diagonal line - callers
// are responsible for only calling this on such a line) is empty.
[[nodiscard]] bool SlidingPathClear(const BoardState& board, int from, int to);

// Pure geometric reachability for every piece type except Pawn (whose reachability depends on
// direction of travel - see PawnCanReach) - doesn't check whose turn it is or whether `to` is
// occupied by a friendly piece.
[[nodiscard]] bool CanPieceReach(const BoardState& board, PieceType type, int from, int to);

[[nodiscard]] bool PawnCanReach(const BoardState& board, int from, int to, PieceColor color, bool isCapture, std::optional<int> enPassantTarget);

// True if any piece of byColor can move to square right now (pure attack geometry - doesn't
// account for that piece being pinned, since a pinned piece still attacks the squares it
// threatens, it just can't legally move there itself).
[[nodiscard]] bool IsSquareAttacked(const BoardState& board, int square, PieceColor byColor);

[[nodiscard]] std::optional<int> FindKing(const BoardState& board, PieceColor color);

// Mutates board in place: moves the piece from `from` to `to` (promoting it if `promotion` is
// set), and removes the captured pawn at enPassantCaptureSquare if given. Doesn't validate
// legality - callers must only call this with an already-legal (or scratch/simulated) move.
void ApplyMoveOnBoard(BoardState& board, int from, int to, std::optional<PieceType> promotion, std::optional<int> enPassantCaptureSquare);

// Has-moved castling-rights forfeiture for one applied move: a king move forfeits both rights
// for its side; either end of the move landing on a rook's home square (it moved away, or was
// just captured there) forfeits that specific right - checked unconditionally against `from`/
// `to` regardless of movedType, since a captured rook's own type never appears here as the
// mover. movedColor is only consulted for the King branch.
void ForfeitCastlingRightsForMove(CastlingRights& rights, PieceType movedType, PieceColor movedColor, int from, int to);
}  // namespace ChessBoardOps
