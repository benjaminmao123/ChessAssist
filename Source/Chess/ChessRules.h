#pragma once

#include "ChessTypes.h"

#include <optional>
#include <string>
#include <string_view>

// Translates one SAN move token (e.g. "Nf3", "exd5", "O-O", "e8=Q+") into a UCI move string.
// Not a full legal-move engine - no check/mate/stalemate detection or castling legality
// (Stockfish re-derives those); the only exception is a pin tie-break used when plain
// reachability leaves multiple candidates. Callers must pass isolated SAN tokens only, no
// move numbers or turn markers.
class ChessRules
{
public:
    void Reset();

    // Applies one SAN move for the side to move. Returns the equivalent UCI move ("g1f3",
    // "e7e8q", "e1g1" for castling) and advances state on success; returns nullopt with state
    // UNCHANGED if the token can't be parsed or resolves to zero/multiple candidates (treat as
    // a hard desync).
    [[nodiscard]] std::optional<std::string> ApplySanMove(std::string_view san);

    [[nodiscard]] PieceColor GetSideToMove() const;
    [[nodiscard]] const BoardState& GetBoard() const;

    // Which castling rights are still available (has-moved bookkeeping; moving back doesn't
    // restore a forfeited right). Does NOT mean castling is legal right now - see class comment.
    [[nodiscard]] CastlingRights GetCastlingRights() const;

    // Square a pawn just double-pushed past, if the last move was a double push, else nullopt.
    // Matches FEN's raw field - not a guarantee an enemy pawn can actually capture there;
    // callers needing real capturability (e.g. Polyglot hashing) must check the board.
    [[nodiscard]] std::optional<int> GetEnPassantTarget() const;

    // Square index of the side-to-move's king if currently attacked, else nullopt. Not a full
    // checkmate/stalemate detector - just whether that one square is attacked right now.
    [[nodiscard]] std::optional<int> CheckedKingSquare() const;

private:
    [[nodiscard]] std::optional<std::string> ApplyCastle(bool kingside);

    BoardState m_Board{};
    PieceColor m_SideToMove = PieceColor::White;
    std::optional<int> m_EnPassantTarget;  // square a pawn just double-pushed past, if any
    CastlingRights m_Rights;
};
