#include "MoveGenerator.h"

#include "ChessBoardOps.h"

#include <array>
#include <cmath>
#include <span>
#include <utility>

namespace
{
using LegalMove = MoveGenerator::LegalMove;
using PositionState = MoveGenerator::PositionState;

bool OnBoard(int file, int rank)
{
    return file >= 0 && file < 8 && rank >= 0 && rank < 8;
}

// Simulates move on a scratch copy of position.Board and checks that it doesn't leave the
// mover's own king attacked - the same "copy, apply, check king safety" pattern
// ChessRules::ApplySanMove's pin tie-break already uses (see its comment), generalized here to
// every candidate move rather than only genuinely SAN-ambiguous ones.
bool LeavesOwnKingSafe(const PositionState& position, const LegalMove& move)
{
    BoardState scratch = position.Board;

    std::optional<int> epCaptureSquare;
    if (move.IsEnPassant)
        epCaptureSquare = ChessBoardOps::EnPassantCaptureSquare(move.From, move.To);

    ChessBoardOps::ApplyMoveOnBoard(scratch, move.From, move.To, move.Promotion, epCaptureSquare);

    const std::optional<int> kingSquare = ChessBoardOps::FindKing(scratch, position.SideToMove);
    return kingSquare.has_value() && !ChessBoardOps::IsSquareAttacked(scratch, *kingSquare, ChessBoardOps::Opposite(position.SideToMove));
}

void AddIfSafe(const PositionState& position, const LegalMove& move, std::vector<LegalMove>& out)
{
    if (LeavesOwnKingSafe(position, move))
        out.push_back(move);
}

void AddPawnMoves(const PositionState& position, int from, std::vector<LegalMove>& out)
{
    const PieceColor color = position.SideToMove;
    const int direction = (color == PieceColor::White) ? 1 : -1;
    const int startRank = (color == PieceColor::White) ? 1 : 6;
    const int promoRank = (color == PieceColor::White) ? 7 : 0;
    const int file = from % 8;
    const int rank = from / 8;

    const auto addMaybePromoting = [&](int to, bool isEnPassant)
    {
        if (to / 8 == promoRank)
        {
            for (PieceType promo : {PieceType::Queen, PieceType::Rook, PieceType::Bishop, PieceType::Knight})
                AddIfSafe(position, LegalMove{from, to, promo, false, isEnPassant}, out);
        }
        else
        {
            AddIfSafe(position, LegalMove{from, to, std::nullopt, false, isEnPassant}, out);
        }
    };

    // Single/double push.
    if (OnBoard(file, rank + direction) && !position.Board[SquareIndex(file, rank + direction)])
    {
        addMaybePromoting(SquareIndex(file, rank + direction), false);

        if (rank == startRank && !position.Board[SquareIndex(file, rank + 2 * direction)])
            AddIfSafe(position, LegalMove{from, SquareIndex(file, rank + 2 * direction), std::nullopt, false, false}, out);
    }

    // Captures, including en passant.
    for (int deltaFile : {-1, 1})
    {
        const int captureFile = file + deltaFile;
        const int captureRank = rank + direction;
        if (!OnBoard(captureFile, captureRank))
            continue;

        const int to = SquareIndex(captureFile, captureRank);
        if (position.Board[to] && position.Board[to]->Color != color)
            addMaybePromoting(to, false);
        else if (!position.Board[to] && position.EnPassantTarget && *position.EnPassantTarget == to)
            AddIfSafe(position, LegalMove{from, to, std::nullopt, false, true}, out);
    }
}

void AddOffsetMoves(const PositionState& position, int from, std::span<const std::pair<int, int>> offsets, std::vector<LegalMove>& out)
{
    const int file = from % 8;
    const int rank = from / 8;
    const PieceColor color = position.SideToMove;

    for (const auto& [deltaFile, deltaRank] : offsets)
    {
        const int toFile = file + deltaFile;
        const int toRank = rank + deltaRank;
        if (!OnBoard(toFile, toRank))
            continue;

        const int to = SquareIndex(toFile, toRank);
        if (position.Board[to] && position.Board[to]->Color == color)
            continue;

        AddIfSafe(position, LegalMove{from, to, std::nullopt, false, false}, out);
    }
}

void AddSlidingMoves(const PositionState& position, int from, std::span<const std::pair<int, int>> directions, std::vector<LegalMove>& out)
{
    const int file = from % 8;
    const int rank = from / 8;
    const PieceColor color = position.SideToMove;

    for (const auto& [deltaFile, deltaRank] : directions)
    {
        int toFile = file + deltaFile;
        int toRank = rank + deltaRank;
        while (OnBoard(toFile, toRank))
        {
            const int to = SquareIndex(toFile, toRank);
            if (position.Board[to])
            {
                if (position.Board[to]->Color != color)
                    AddIfSafe(position, LegalMove{from, to, std::nullopt, false, false}, out);
                break;
            }

            AddIfSafe(position, LegalMove{from, to, std::nullopt, false, false}, out);
            toFile += deltaFile;
            toRank += deltaRank;
        }
    }
}

void AddPieceMoves(const PositionState& position, PieceType type, int from, std::vector<LegalMove>& out)
{
    static constexpr std::array<std::pair<int, int>, 8> kKnightOffsets{{{1, 2}, {2, 1}, {2, -1}, {1, -2}, {-1, -2}, {-2, -1}, {-2, 1}, {-1, 2}}};
    static constexpr std::array<std::pair<int, int>, 8> kKingOffsets{{{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}}};
    static constexpr std::array<std::pair<int, int>, 4> kBishopDirs{{{1, 1}, {1, -1}, {-1, 1}, {-1, -1}}};
    static constexpr std::array<std::pair<int, int>, 4> kRookDirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

    switch (type)
    {
    case PieceType::Pawn:
        AddPawnMoves(position, from, out);
        break;
    case PieceType::Knight:
        AddOffsetMoves(position, from, kKnightOffsets, out);
        break;
    case PieceType::King:
        AddOffsetMoves(position, from, kKingOffsets, out);
        break;
    case PieceType::Bishop:
        AddSlidingMoves(position, from, kBishopDirs, out);
        break;
    case PieceType::Rook:
        AddSlidingMoves(position, from, kRookDirs, out);
        break;
    case PieceType::Queen:
        AddSlidingMoves(position, from, kBishopDirs, out);
        AddSlidingMoves(position, from, kRookDirs, out);
        break;
    }
}

// Genuinely new legality logic (not reused from ChessRules - see MoveGenerator.h's class
// comment): rights, empty-path occupancy, and that the king's current, transit, and
// destination squares are all unattacked. Queenside castling only requires the transit/
// destination squares (c/d-file) to be unattacked, not the whole empty path (b-file doesn't
// need to be safe), matching standard chess rules.
void AddCastlingMoves(const PositionState& position, std::vector<LegalMove>& out)
{
    const PieceColor color = position.SideToMove;
    const int rank = (color == PieceColor::White) ? 0 : 7;
    const int kingFrom = SquareIndex(4, rank);

    const std::optional<Piece>& king = position.Board[kingFrom];
    if (!king || king->Type != PieceType::King || king->Color != color)
        return;

    const PieceColor enemy = ChessBoardOps::Opposite(color);
    if (ChessBoardOps::IsSquareAttacked(position.Board, kingFrom, enemy))
        return;  // can't castle out of check

    const auto tryOne = [&](bool kingside)
    {
        const bool hasRight = kingside ? (color == PieceColor::White ? position.Rights.WhiteKingside : position.Rights.BlackKingside)
                                        : (color == PieceColor::White ? position.Rights.WhiteQueenside : position.Rights.BlackQueenside);
        if (!hasRight)
            return;

        const int rookFrom = SquareIndex(kingside ? 7 : 0, rank);
        const std::optional<Piece>& rook = position.Board[rookFrom];
        if (!rook || rook->Type != PieceType::Rook || rook->Color != color)
            return;

        const int emptyStart = kingside ? 5 : 1;
        const int emptyEnd = kingside ? 6 : 3;  // inclusive - every file strictly between king and rook
        for (int f = emptyStart; f <= emptyEnd; ++f)
        {
            if (position.Board[SquareIndex(f, rank)])
                return;
        }

        const int transitSquare = SquareIndex(kingside ? 5 : 3, rank);
        const int kingTo = SquareIndex(kingside ? 6 : 2, rank);
        if (ChessBoardOps::IsSquareAttacked(position.Board, transitSquare, enemy) || ChessBoardOps::IsSquareAttacked(position.Board, kingTo, enemy))
            return;

        out.push_back(LegalMove{kingFrom, kingTo, std::nullopt, true, false});
    };

    tryOne(true);
    tryOne(false);
}
}  // namespace

std::vector<MoveGenerator::LegalMove> MoveGenerator::GenerateLegalMoves(const PositionState& position)
{
    std::vector<LegalMove> moves;

    for (int idx = 0; idx < 64; ++idx)
    {
        const std::optional<Piece>& piece = position.Board[idx];
        if (!piece || piece->Color != position.SideToMove)
            continue;

        AddPieceMoves(position, piece->Type, idx, moves);
    }

    AddCastlingMoves(position, moves);

    return moves;
}

std::vector<MoveGenerator::LegalMove> MoveGenerator::GenerateLegalMovesFrom(const PositionState& position, int from)
{
    std::vector<LegalMove> filtered;
    for (LegalMove& move : GenerateLegalMoves(position))
    {
        if (move.From == from)
            filtered.push_back(move);
    }
    return filtered;
}

std::optional<MoveGenerator::LegalMove> MoveGenerator::FindLegalMove(const PositionState& position, const std::string& uciMove)
{
    for (const LegalMove& move : GenerateLegalMoves(position))
    {
        if (ToUci(move) == uciMove)
            return move;
    }

    return std::nullopt;
}

bool MoveGenerator::VerifyTwoPlyContinuation(const PositionState& position, const std::string& firstUci, const std::string& secondUci)
{
    const std::optional<LegalMove> first = FindLegalMove(position, firstUci);
    if (!first)
        return false;

    PositionState next = position;
    ApplyMove(next, *first);

    return FindLegalMove(next, secondUci).has_value();
}

void MoveGenerator::ApplyMove(PositionState& position, const LegalMove& move)
{
    if (move.IsCastle)
    {
        const int rank = move.From / 8;
        const bool kingside = move.To > move.From;
        const int rookFrom = SquareIndex(kingside ? 7 : 0, rank);
        const int rookTo = SquareIndex(kingside ? 5 : 3, rank);

        ChessBoardOps::ApplyCastleOnBoard(position.Board, move.From, move.To, rookFrom, rookTo);

        ChessBoardOps::ForfeitCastlingRightsForMove(position.Rights, PieceType::King, position.SideToMove, move.From, move.To);
        position.EnPassantTarget.reset();
        position.SideToMove = ChessBoardOps::Opposite(position.SideToMove);
        return;
    }

    const PieceType movedType = position.Board[move.From]->Type;

    std::optional<int> epCaptureSquare;
    if (move.IsEnPassant)
        epCaptureSquare = ChessBoardOps::EnPassantCaptureSquare(move.From, move.To);

    ChessBoardOps::ApplyMoveOnBoard(position.Board, move.From, move.To, move.Promotion, epCaptureSquare);
    ChessBoardOps::ForfeitCastlingRightsForMove(position.Rights, movedType, position.SideToMove, move.From, move.To);

    position.EnPassantTarget.reset();
    if (movedType == PieceType::Pawn && std::abs(move.To / 8 - move.From / 8) == 2)
        position.EnPassantTarget = SquareIndex(move.To % 8, (move.From / 8 + move.To / 8) / 2);

    position.SideToMove = ChessBoardOps::Opposite(position.SideToMove);
}

bool MoveGenerator::HasNoLegalMoves(const PositionState& position)
{
    return GenerateLegalMoves(position).empty();
}

std::string MoveGenerator::ToUci(const LegalMove& move)
{
    std::string uci = SquareToAlgebraic(move.From) + SquareToAlgebraic(move.To);
    if (move.Promotion)
    {
        switch (*move.Promotion)
        {
        case PieceType::Queen:
            uci += 'q';
            break;
        case PieceType::Rook:
            uci += 'r';
            break;
        case PieceType::Bishop:
            uci += 'b';
            break;
        case PieceType::Knight:
            uci += 'n';
            break;
        default:
            break;
        }
    }
    return uci;
}
