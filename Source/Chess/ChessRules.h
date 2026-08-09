#pragma once

#include "ChessTypes.h"

#include <optional>
#include <string>
#include <string_view>

// Translates one SAN move token (as rendered by a chess site's move list, e.g. "Nf3",
// "exd5", "O-O", "e8=Q+") into a UCI move string, tracking just enough internal board state
// to disambiguate which piece a token refers to. This is deliberately NOT a full legal-move
// engine - no check/checkmate/stalemate detection, no castling-rights tracking (Stockfish
// re-derives real castling legality from the full move list it's given, so ChessRules only
// needs to know which piece a token refers to) - except for a narrow, on-demand pin
// tie-break used only when plain geometric reachability leaves more than one candidate.
//
// Callers must feed it isolated SAN tokens only (e.g. "Nf3", not "3...Nf3" or "1.e4") - move
// numbers and turn markers are the DOM-extraction layer's job to strip, not this class's.
class ChessRules
{
public:
    void Reset();

    // Applies one SAN move for the side currently to move. Returns the equivalent UCI move
    // ("g1f3", "e7e8q", "e1g1" for castling) and advances internal state on success. Returns
    // nullopt with state left UNCHANGED if the token can't be parsed, or resolves to zero or
    // more than one legal candidate piece - callers must treat that as a hard desync.
    [[nodiscard]] std::optional<std::string> ApplySanMove(std::string_view san);

    [[nodiscard]] PieceColor GetSideToMove() const;
    [[nodiscard]] const BoardState& GetBoard() const;

    // Which castling rights are still available (has-moved bookkeeping only - king/rook moving
    // and moving back still permanently forfeits the right, matching real chess and standard
    // FEN semantics). Does NOT mean castling is legal right now (through-check etc. is still
    // Stockfish's job, same as everywhere else in this class) - see the class comment.
    [[nodiscard]] CastlingRights GetCastlingRights() const;

    // The square a pawn just double-pushed past, if the last move applied was a double push -
    // else nullopt. This is the raw "last move was a double push" target (matches FEN's own
    // pre-refinement field), NOT a guarantee some enemy pawn can actually capture there this
    // instant - callers that need real capturability (e.g. Polyglot hashing) must check the
    // board themselves.
    [[nodiscard]] std::optional<int> GetEnPassantTarget() const;

    // Square index of the side-to-move's king if it's currently attacked, else nullopt -
    // reuses the same square-attack helper the pin tie-break above already relies on. Not a
    // full checkmate/stalemate detector (this class deliberately isn't one - see the class
    // comment), just "is this one square attacked right now".
    [[nodiscard]] std::optional<int> CheckedKingSquare() const;

private:
    [[nodiscard]] std::optional<std::string> ApplyCastle(bool kingside);

    BoardState m_Board{};
    PieceColor m_SideToMove = PieceColor::White;
    std::optional<int> m_EnPassantTarget;  // square a pawn just double-pushed past, if any
    CastlingRights m_Rights;
};
