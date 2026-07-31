#include "MoveDetector.h"

#include <vector>

namespace
{
struct SquareChange
{
    int Index;
    std::optional<Piece> Before;
    std::optional<Piece> After;
};

std::vector<SquareChange> ComputeChanges(const BoardState& before, const BoardState& after)
{
    std::vector<SquareChange> changes;

    for (int index = 0; index < 64; ++index)
    {
        if (before[index] != after[index])
            changes.push_back({index, before[index], after[index]});
    }

    return changes;
}

template <typename Predicate>
std::optional<SquareChange> FindUnique(const std::vector<SquareChange>& changes, Predicate match)
{
    std::optional<SquareChange> found;

    for (const SquareChange& change : changes)
    {
        if (!match(change))
            continue;

        if (found)
            return std::nullopt;  // ambiguous - more than one match

        found = change;
    }

    return found;
}

bool IsMoverVanishing(const SquareChange& change, PieceColor sideToMove)
{
    return change.Before && change.Before->Color == sideToMove && !change.After;
}

bool IsMoverAppearing(const SquareChange& change, PieceColor sideToMove)
{
    return change.After && change.After->Color == sideToMove;
}

char PromotionSuffix(PieceType type)
{
    switch (type)
    {
    case PieceType::Queen:
        return 'q';
    case PieceType::Rook:
        return 'r';
    case PieceType::Bishop:
        return 'b';
    case PieceType::Knight:
        return 'n';
    default:
        return '\0';
    }
}

std::optional<std::string> DetectSimpleMove(const std::vector<SquareChange>& changes, PieceColor sideToMove)
{
    const std::optional<SquareChange> from = FindUnique(changes, [sideToMove](const SquareChange& change) { return IsMoverVanishing(change, sideToMove); });
    const std::optional<SquareChange> to = FindUnique(changes, [sideToMove](const SquareChange& change) { return IsMoverAppearing(change, sideToMove); });

    if (!from || !to || from->Index == to->Index)
        return std::nullopt;

    std::string move = SquareToAlgebraic(from->Index) + SquareToAlgebraic(to->Index);

    if (from->Before->Type == PieceType::Pawn && to->After->Type != PieceType::Pawn)
    {
        const char suffix = PromotionSuffix(to->After->Type);
        if (suffix != '\0')
            move += suffix;
    }

    return move;
}

std::optional<std::string> DetectEnPassant(const std::vector<SquareChange>& changes, PieceColor sideToMove)
{
    const std::optional<SquareChange> from = FindUnique(changes, [sideToMove](const SquareChange& change) {
        return IsMoverVanishing(change, sideToMove) && change.Before->Type == PieceType::Pawn;
    });

    const std::optional<SquareChange> to = FindUnique(changes, [sideToMove](const SquareChange& change) {
        return IsMoverAppearing(change, sideToMove) && change.After->Type == PieceType::Pawn && !change.Before;
    });

    if (!from || !to)
        return std::nullopt;

    const std::optional<SquareChange> captured = FindUnique(changes, [sideToMove, &from](const SquareChange& change) {
        if (change.Index == from->Index)
            return false;

        return change.Before && change.Before->Color != sideToMove && change.Before->Type == PieceType::Pawn && !change.After;
    });

    if (!captured)
        return std::nullopt;

    const int fromFile = from->Index % 8;
    const int fromRank = from->Index / 8;
    const int toFile = to->Index % 8;
    const int capturedFile = captured->Index % 8;
    const int capturedRank = captured->Index / 8;

    if (capturedFile != toFile || capturedRank != fromRank || toFile == fromFile)
        return std::nullopt;

    return SquareToAlgebraic(from->Index) + SquareToAlgebraic(to->Index);
}

std::optional<std::string> DetectCastling(const std::vector<SquareChange>& changes, PieceColor sideToMove)
{
    const std::optional<SquareChange> kingFrom = FindUnique(changes, [sideToMove](const SquareChange& change) {
        return IsMoverVanishing(change, sideToMove) && change.Before->Type == PieceType::King;
    });

    const std::optional<SquareChange> kingTo = FindUnique(changes, [sideToMove](const SquareChange& change) {
        return IsMoverAppearing(change, sideToMove) && change.After->Type == PieceType::King && !change.Before;
    });

    if (!kingFrom || !kingTo)
        return std::nullopt;

    const std::optional<SquareChange> rookFrom = FindUnique(changes, [sideToMove, &kingFrom](const SquareChange& change) {
        return change.Index != kingFrom->Index && IsMoverVanishing(change, sideToMove) && change.Before->Type == PieceType::Rook;
    });

    const std::optional<SquareChange> rookTo = FindUnique(changes, [sideToMove, &kingTo](const SquareChange& change) {
        return change.Index != kingTo->Index && IsMoverAppearing(change, sideToMove) && change.After->Type == PieceType::Rook && !change.Before;
    });

    if (!rookFrom || !rookTo)
        return std::nullopt;

    return SquareToAlgebraic(kingFrom->Index) + SquareToAlgebraic(kingTo->Index);
}
}  // namespace

namespace MoveDetector
{
std::optional<std::string> DetectMove(const BoardState& before, const BoardState& after, PieceColor sideToMove)
{
    const std::vector<SquareChange> changes = ComputeChanges(before, after);

    switch (changes.size())
    {
    case 2:
        return DetectSimpleMove(changes, sideToMove);
    case 3:
        return DetectEnPassant(changes, sideToMove);
    case 4:
        return DetectCastling(changes, sideToMove);
    default:
        return std::nullopt;
    }
}
}  // namespace MoveDetector
