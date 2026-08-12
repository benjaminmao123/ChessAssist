#include "ChessBoardOps.h"

#include <cmath>

namespace ChessBoardOps
{
PieceColor Opposite(PieceColor color)
{
    return color == PieceColor::White ? PieceColor::Black : PieceColor::White;
}

bool SlidingPathClear(const BoardState& board, int from, int to)
{
    const int fromFile = from % 8, fromRank = from / 8;
    const int toFile = to % 8, toRank = to / 8;
    const int stepFile = (toFile > fromFile) ? 1 : (toFile < fromFile ? -1 : 0);
    const int stepRank = (toRank > fromRank) ? 1 : (toRank < fromRank ? -1 : 0);

    int file = fromFile + stepFile;
    int rank = fromRank + stepRank;
    while (file != toFile || rank != toRank)
    {
        if (board[SquareIndex(file, rank)])
            return false;
        file += stepFile;
        rank += stepRank;
    }

    return true;
}

bool CanPieceReach(const BoardState& board, PieceType type, int from, int to)
{
    if (from == to)
        return false;

    const int fromFile = from % 8, fromRank = from / 8;
    const int toFile = to % 8, toRank = to / 8;
    const int dFile = toFile - fromFile;
    const int dRank = toRank - fromRank;

    switch (type)
    {
    case PieceType::Knight:
        return (std::abs(dFile) == 1 && std::abs(dRank) == 2) || (std::abs(dFile) == 2 && std::abs(dRank) == 1);

    case PieceType::King:
        return std::abs(dFile) <= 1 && std::abs(dRank) <= 1;

    case PieceType::Bishop:
        return std::abs(dFile) == std::abs(dRank) && SlidingPathClear(board, from, to);

    case PieceType::Rook:
        return (dFile == 0 || dRank == 0) && SlidingPathClear(board, from, to);

    case PieceType::Queen:
        return (std::abs(dFile) == std::abs(dRank) || dFile == 0 || dRank == 0) && SlidingPathClear(board, from, to);

    case PieceType::Pawn:
        return false;
    }

    return false;
}

bool PawnCanReach(const BoardState& board, int from, int to, PieceColor color, bool isCapture, std::optional<int> enPassantTarget)
{
    const int fromFile = from % 8, fromRank = from / 8;
    const int toFile = to % 8, toRank = to / 8;
    const int direction = (color == PieceColor::White) ? 1 : -1;
    const int startRank = (color == PieceColor::White) ? 1 : 6;

    if (isCapture)
    {
        if (toRank - fromRank != direction || std::abs(toFile - fromFile) != 1)
            return false;

        if (board[to] && board[to]->Color != color)
            return true;

        return !board[to] && enPassantTarget && *enPassantTarget == to;
    }

    if (toFile != fromFile || board[to])
        return false;

    if (toRank - fromRank == direction)
        return true;

    if (fromRank == startRank && toRank - fromRank == 2 * direction)
        return !board[SquareIndex(fromFile, fromRank + direction)];

    return false;
}

bool IsSquareAttacked(const BoardState& board, int square, PieceColor byColor)
{
    const int targetFile = square % 8;
    const int targetRank = square / 8;

    for (int idx = 0; idx < 64; ++idx)
    {
        const std::optional<Piece>& piece = board[idx];
        if (!piece || piece->Color != byColor)
            continue;

        if (piece->Type == PieceType::Pawn)
        {
            const int direction = (byColor == PieceColor::White) ? 1 : -1;
            const int file = idx % 8, rank = idx / 8;
            if (rank + direction == targetRank && std::abs(file - targetFile) == 1)
                return true;
            continue;
        }

        if (CanPieceReach(board, piece->Type, idx, square))
            return true;
    }

    return false;
}

std::optional<int> FindKing(const BoardState& board, PieceColor color)
{
    for (int idx = 0; idx < 64; ++idx)
    {
        if (board[idx] && board[idx]->Type == PieceType::King && board[idx]->Color == color)
            return idx;
    }

    return std::nullopt;
}

void ApplyMoveOnBoard(BoardState& board, int from, int to, std::optional<PieceType> promotion, std::optional<int> enPassantCaptureSquare)
{
    Piece moving = *board[from];
    if (promotion)
        moving.Type = *promotion;

    board[from] = std::nullopt;
    if (enPassantCaptureSquare)
        board[*enPassantCaptureSquare] = std::nullopt;
    board[to] = moving;
}

void ForfeitCastlingRightsForMove(CastlingRights& rights, PieceType movedType, PieceColor movedColor, int from, int to)
{
    if (movedType == PieceType::King)
    {
        if (movedColor == PieceColor::White)
            rights.WhiteKingside = rights.WhiteQueenside = false;
        else
            rights.BlackKingside = rights.BlackQueenside = false;
    }
    if (from == SquareIndex(0, 0) || to == SquareIndex(0, 0))
        rights.WhiteQueenside = false;
    if (from == SquareIndex(7, 0) || to == SquareIndex(7, 0))
        rights.WhiteKingside = false;
    if (from == SquareIndex(0, 7) || to == SquareIndex(0, 7))
        rights.BlackQueenside = false;
    if (from == SquareIndex(7, 7) || to == SquareIndex(7, 7))
        rights.BlackKingside = false;
}
}  // namespace ChessBoardOps
