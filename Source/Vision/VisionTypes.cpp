#include "VisionTypes.h"

std::string SquareToAlgebraic(int index)
{
    const int file = index % 8;
    const int rank = index / 8;

    std::string result;
    result += static_cast<char>('a' + file);
    result += static_cast<char>('1' + rank);
    return result;
}

std::optional<Piece> GetStandardStartingPiece(int file, int rank)
{
    if (rank == 1)
        return Piece{PieceType::Pawn, PieceColor::White};

    if (rank == 6)
        return Piece{PieceType::Pawn, PieceColor::Black};

    if (rank != 0 && rank != 7)
        return std::nullopt;

    const PieceColor color = (rank == 0) ? PieceColor::White : PieceColor::Black;

    switch (file)
    {
    case 0:
    case 7:
        return Piece{PieceType::Rook, color};
    case 1:
    case 6:
        return Piece{PieceType::Knight, color};
    case 2:
    case 5:
        return Piece{PieceType::Bishop, color};
    case 3:
        return Piece{PieceType::Queen, color};
    case 4:
        return Piece{PieceType::King, color};
    default:
        return std::nullopt;
    }
}
