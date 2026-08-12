#include "FenWriter.h"

#include <cctype>

namespace
{
char PieceLetter(const Piece& piece)
{
    char letter = '?';
    switch (piece.Type)
    {
    case PieceType::Pawn:
        letter = 'p';
        break;
    case PieceType::Knight:
        letter = 'n';
        break;
    case PieceType::Bishop:
        letter = 'b';
        break;
    case PieceType::Rook:
        letter = 'r';
        break;
    case PieceType::Queen:
        letter = 'q';
        break;
    case PieceType::King:
        letter = 'k';
        break;
    }

    return piece.Color == PieceColor::White ? static_cast<char>(std::toupper(static_cast<unsigned char>(letter))) : letter;
}
}  // namespace

std::string ToFen(const BoardState& board, PieceColor sideToMove, CastlingRights rights, std::optional<int> enPassantTarget)
{
    std::string fen;

    for (int rank = 7; rank >= 0; --rank)
    {
        int emptyRun = 0;
        for (int file = 0; file < 8; ++file)
        {
            const std::optional<Piece>& piece = board[SquareIndex(file, rank)];
            if (!piece)
            {
                ++emptyRun;
                continue;
            }

            if (emptyRun > 0)
            {
                fen += std::to_string(emptyRun);
                emptyRun = 0;
            }
            fen += PieceLetter(*piece);
        }

        if (emptyRun > 0)
            fen += std::to_string(emptyRun);
        if (rank > 0)
            fen += '/';
    }

    fen += ' ';
    fen += (sideToMove == PieceColor::White) ? 'w' : 'b';

    fen += ' ';
    std::string castling;
    if (rights.WhiteKingside)
        castling += 'K';
    if (rights.WhiteQueenside)
        castling += 'Q';
    if (rights.BlackKingside)
        castling += 'k';
    if (rights.BlackQueenside)
        castling += 'q';
    fen += castling.empty() ? "-" : castling;

    fen += ' ';
    fen += enPassantTarget ? SquareToAlgebraic(*enPassantTarget) : "-";

    fen += " 0 1";

    return fen;
}
