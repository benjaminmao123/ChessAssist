#pragma once

#include "ChessTypes.h"

#include <optional>
#include <string>
#include <vector>

// Full legal-move enumeration over an explicit position snapshot - independent of ChessRules,
// which only ever needs to answer "which piece does this SAN token refer to," not "give me
// every legal destination for the piece on e2." Built for the sandbox board's drag-to-move
// interaction, which has no engine call to lean on for legality (unlike ChessRules::ApplyCastle,
// which explicitly defers castling legality to Stockfish - see its header comment).
class MoveGenerator
{
public:
    struct LegalMove
    {
        int From = 0;
        int To = 0;
        std::optional<PieceType> Promotion;  // set on exactly one of the 4 promotion-choice entries
        bool IsCastle = false;
        bool IsEnPassant = false;

        bool operator==(const LegalMove&) const = default;
    };

    struct PositionState
    {
        BoardState Board{};
        PieceColor SideToMove = PieceColor::White;
        CastlingRights Rights{};
        std::optional<int> EnPassantTarget;
    };

    // Every fully legal move (check-safety filtered, not just pseudo-legal reachability) for
    // the side to move. A promoting pawn move expands to 4 entries, one per promotion choice.
    [[nodiscard]] static std::vector<LegalMove> GenerateLegalMoves(const PositionState& position);

    // Convenience filter for board UI: every legal move whose .From == from. Empty if there's
    // no piece there, it's the wrong side to move, or the piece has no legal moves.
    [[nodiscard]] static std::vector<LegalMove> GenerateLegalMovesFrom(const PositionState& position, int from);

    // The legal move in position whose UCI notation is uciMove, if any - resolves a plain UCI
    // string (e.g. from an engine's PV) into an applyable LegalMove, and, as a side effect,
    // validates that the string is actually legal in position at all.
    [[nodiscard]] static std::optional<LegalMove> FindLegalMove(const PositionState& position, const std::string& uciMove);

    // Mutates position in place to the result of playing move: moves the piece (castling also
    // moves the rook; en passant also removes the captured pawn), updates CastlingRights and
    // EnPassantTarget, flips SideToMove. Assumes move is already legal - callers must validate
    // via GenerateLegalMoves/GenerateLegalMovesFrom first.
    static void ApplyMove(PositionState& position, const LegalMove& move);

    // True if the side to move has no legal moves at all - checkmate if currently in check,
    // stalemate otherwise (this function alone doesn't distinguish the two).
    [[nodiscard]] static bool HasNoLegalMoves(const PositionState& position);

    // "e2e4" / "e7e8q" / "e1g1" (castling, king's own from/to square) - UCI move notation.
    [[nodiscard]] static std::string ToUci(const LegalMove& move);
};
