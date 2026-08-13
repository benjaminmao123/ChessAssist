#pragma once

#include "ChessTypes.h"

#include <optional>
#include <string>
#include <vector>

// Full legal-move enumeration over an explicit position snapshot, independent of ChessRules
// (which only resolves SAN tokens to a piece, not full legality). Built for the sandbox
// board's drag-to-move interaction, which has no engine call to lean on for legality.
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
    // string (e.g. from an engine's PV) into an applyable LegalMove, validating legality too.
    [[nodiscard]] static std::optional<LegalMove> FindLegalMove(const PositionState& position, const std::string& uciMove);

    // True if firstUci is legal in position AND secondUci is legal in the resulting position -
    // used to validate a two-ply PV before drawing the second ply as an arrow on the still-
    // one-ply-behind current board, where it could otherwise look illegal (e.g. an en passant
    // capture only reachable after the first ply is played).
    [[nodiscard]] static bool VerifyTwoPlyContinuation(const PositionState& position, const std::string& firstUci, const std::string& secondUci);

    // Mutates position in place to the result of playing move (handles castling rook move and
    // en passant capture, updates rights/en passant/side to move). Assumes move is legal.
    static void ApplyMove(PositionState& position, const LegalMove& move);

    // True if the side to move has no legal moves at all - checkmate if currently in check,
    // stalemate otherwise (this function alone doesn't distinguish the two).
    [[nodiscard]] static bool HasNoLegalMoves(const PositionState& position);

    // "e2e4" / "e7e8q" / "e1g1" (castling, king's own from/to square) - UCI move notation.
    [[nodiscard]] static std::string ToUci(const LegalMove& move);
};
