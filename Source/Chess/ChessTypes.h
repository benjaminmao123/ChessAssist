#pragma once

#include <array>
#include <optional>
#include <string>
#include <string_view>

enum class PieceType
{
    Pawn,
    Knight,
    Bishop,
    Rook,
    Queen,
    King,
};

enum class PieceColor
{
    White,
    Black,
};

struct Piece
{
    PieceType Type;
    PieceColor Color;

    bool operator==(const Piece&) const = default;
};

// Canonical indexing: file 0..7 = a..h, rank 0..7 = rank1..rank8, index = rank * 8 + file,
// independent of screen orientation.
using BoardState = std::array<std::optional<Piece>, 64>;

// Castling "rights" (per FEN's castling field), not current legality - king may still be in
// check or the path blocked. See ChessRules::GetCastlingRights().
struct CastlingRights
{
    bool WhiteKingside = true;
    bool WhiteQueenside = true;
    bool BlackKingside = true;
    bool BlackQueenside = true;
};

enum class BoardOrientation
{
    WhiteBottom,
    BlackBottom,
};

constexpr int SquareIndex(int file, int rank)
{
    return rank * 8 + file;
}

constexpr bool IsLightSquare(int file, int rank)
{
    return (file + rank) % 2 != 0;
}

std::string SquareToAlgebraic(int index);

std::optional<Piece> GetStandardStartingPiece(int file, int rank);

inline constexpr std::string_view kStandardStartFen = "rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1";
