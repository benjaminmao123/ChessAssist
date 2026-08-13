#include "ChessRules.h"

#include "ChessBoardOps.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
struct ParsedSan
{
    PieceType Type = PieceType::Pawn;
    bool IsCapture = false;
    std::optional<int> HintFile;
    std::optional<int> HintRank;
    int DestFile = 0;
    int DestRank = 0;
    std::optional<PieceType> Promotion;
};

std::optional<PieceType> PieceTypeFromLetter(char c)
{
    switch (c)
    {
    case 'K':
        return PieceType::King;
    case 'Q':
        return PieceType::Queen;
    case 'R':
        return PieceType::Rook;
    case 'B':
        return PieceType::Bishop;
    case 'N':
        return PieceType::Knight;
    default:
        return std::nullopt;
    }
}

char PromotionLetter(PieceType type)
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

std::optional<ParsedSan> ParseSan(std::string_view san)
{
    if (san.empty())
        return std::nullopt;

    ParsedSan result;

    // Promotion: "=Q" suffix.
    if (const std::size_t equalsPos = san.find('='); equalsPos != std::string_view::npos)
    {
        if (equalsPos + 1 >= san.size())
            return std::nullopt;

        const std::optional<PieceType> promo = PieceTypeFromLetter(san[equalsPos + 1]);
        if (!promo || *promo == PieceType::King)
            return std::nullopt;

        result.Promotion = promo;
        san = san.substr(0, equalsPos);
    }

    if (san.empty())
        return std::nullopt;

    // Leading piece letter (absence = pawn).
    if (const std::optional<PieceType> letterPiece = PieceTypeFromLetter(san.front()))
    {
        result.Type = *letterPiece;
        san.remove_prefix(1);
    }

    // Strip the capture marker (at most one, always immediately before the destination).
    std::string cleaned;
    cleaned.reserve(san.size());
    for (char c : san)
    {
        if (c == 'x' || c == 'X')
            result.IsCapture = true;
        else
            cleaned += c;
    }

    if (cleaned.size() < 2)
        return std::nullopt;

    const char destFileChar = cleaned[cleaned.size() - 2];
    const char destRankChar = cleaned[cleaned.size() - 1];
    if (destFileChar < 'a' || destFileChar > 'h' || destRankChar < '1' || destRankChar > '8')
        return std::nullopt;

    result.DestFile = destFileChar - 'a';
    result.DestRank = destRankChar - '1';

    const std::string_view hintPart(cleaned.data(), cleaned.size() - 2);
    for (char c : hintPart)
    {
        if (c >= 'a' && c <= 'h')
            result.HintFile = c - 'a';
        else if (c >= '1' && c <= '8')
            result.HintRank = c - '1';
        else
            return std::nullopt;
    }

    return result;
}

}  // namespace

void ChessRules::Reset()
{
    m_Board = BoardState{};
    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            m_Board[SquareIndex(file, rank)] = GetStandardStartingPiece(file, rank);

    m_SideToMove = PieceColor::White;
    m_EnPassantTarget.reset();
    m_Rights = CastlingRights{};
}

std::optional<std::string> ChessRules::ApplyCastle(bool kingside)
{
    const int rank = (m_SideToMove == PieceColor::White) ? 0 : 7;
    const int kingFrom = SquareIndex(4, rank);
    const int rookFrom = SquareIndex(kingside ? 7 : 0, rank);
    const int kingTo = SquareIndex(kingside ? 6 : 2, rank);
    const int rookTo = SquareIndex(kingside ? 5 : 3, rank);

    const std::optional<Piece> king = m_Board[kingFrom];
    const std::optional<Piece> rook = m_Board[rookFrom];
    if (!king || king->Type != PieceType::King || king->Color != m_SideToMove)
        return std::nullopt;
    if (!rook || rook->Type != PieceType::Rook || rook->Color != m_SideToMove)
        return std::nullopt;

    ChessBoardOps::ApplyCastleOnBoard(m_Board, kingFrom, kingTo, rookFrom, rookTo);

    m_EnPassantTarget.reset();

    ChessBoardOps::ForfeitCastlingRightsForMove(m_Rights, PieceType::King, m_SideToMove, kingFrom, kingTo);

    m_SideToMove = ChessBoardOps::Opposite(m_SideToMove);

    return SquareToAlgebraic(kingFrom) + SquareToAlgebraic(kingTo);
}

std::optional<std::string> ChessRules::ApplySanMove(std::string_view sanInput)
{
    while (!sanInput.empty() && std::isspace(static_cast<unsigned char>(sanInput.front())))
        sanInput.remove_prefix(1);
    while (!sanInput.empty() && std::isspace(static_cast<unsigned char>(sanInput.back())))
        sanInput.remove_suffix(1);

    while (!sanInput.empty() && (sanInput.back() == '+' || sanInput.back() == '#'))
        sanInput.remove_suffix(1);

    if (sanInput.empty())
        return std::nullopt;

    if (sanInput == "O-O" || sanInput == "0-0")
        return ApplyCastle(true);
    if (sanInput == "O-O-O" || sanInput == "0-0-0")
        return ApplyCastle(false);

    const std::optional<ParsedSan> parsed = ParseSan(sanInput);
    if (!parsed)
        return std::nullopt;

    const int destIndex = SquareIndex(parsed->DestFile, parsed->DestRank);

    std::vector<int> candidates;
    for (int idx = 0; idx < 64; ++idx)
    {
        const std::optional<Piece>& occupant = m_Board[idx];
        if (!occupant || occupant->Color != m_SideToMove || occupant->Type != parsed->Type)
            continue;

        const int file = idx % 8;
        const int rank = idx / 8;
        if (parsed->HintFile && *parsed->HintFile != file)
            continue;
        if (parsed->HintRank && *parsed->HintRank != rank)
            continue;

        if (parsed->Type == PieceType::Pawn)
        {
            if (!ChessBoardOps::PawnCanReach(m_Board, idx, destIndex, m_SideToMove, parsed->IsCapture, m_EnPassantTarget))
                continue;
        }
        else
        {
            if (m_Board[destIndex] && m_Board[destIndex]->Color == m_SideToMove)
                continue;
            if (!ChessBoardOps::CanPieceReach(m_Board, parsed->Type, idx, destIndex))
                continue;
        }

        candidates.push_back(idx);
    }

    if (candidates.empty())
        return std::nullopt;

    if (candidates.size() > 1)
    {
        // Geometric reachability alone can leave multiple candidates when one is pinned to its
        // own king; resolve by simulating each and keeping only ones that don't self-check.
        std::vector<int> legal;
        for (int candidate : candidates)
        {
            const bool isEnPassant = parsed->Type == PieceType::Pawn && parsed->IsCapture && !m_Board[destIndex];
            const std::optional<int> epCapture = isEnPassant ? std::optional<int>(ChessBoardOps::EnPassantCaptureSquare(candidate, destIndex)) : std::nullopt;

            BoardState scratch = m_Board;
            ChessBoardOps::ApplyMoveOnBoard(scratch, candidate, destIndex, parsed->Promotion, epCapture);

            const std::optional<int> kingSquare = ChessBoardOps::FindKing(scratch, m_SideToMove);
            if (kingSquare && !ChessBoardOps::IsSquareAttacked(scratch, *kingSquare, ChessBoardOps::Opposite(m_SideToMove)))
                legal.push_back(candidate);
        }

        if (legal.size() != 1)
            return std::nullopt;

        candidates = legal;
    }

    const int source = candidates.front();

    std::optional<int> enPassantCaptureSquare;
    if (parsed->Type == PieceType::Pawn && parsed->IsCapture && !m_Board[destIndex])
        enPassantCaptureSquare = ChessBoardOps::EnPassantCaptureSquare(source, destIndex);

    ChessBoardOps::ApplyMoveOnBoard(m_Board, source, destIndex, parsed->Promotion, enPassantCaptureSquare);

    // A king move forfeits both rights; a rook's home square being vacated or landed on (i.e.
    // captured there) forfeits that one right - checked unconditionally since destIndex can be
    // a rook's home square regardless of which piece type captured it.
    ChessBoardOps::ForfeitCastlingRightsForMove(m_Rights, parsed->Type, m_SideToMove, source, destIndex);

    m_EnPassantTarget.reset();
    if (parsed->Type == PieceType::Pawn && std::abs(destIndex / 8 - source / 8) == 2)
        m_EnPassantTarget = SquareIndex(parsed->DestFile, (source / 8 + destIndex / 8) / 2);

    m_SideToMove = ChessBoardOps::Opposite(m_SideToMove);

    std::string uci = SquareToAlgebraic(source) + SquareToAlgebraic(destIndex);
    if (parsed->Promotion)
        uci += PromotionLetter(*parsed->Promotion);

    return uci;
}

PieceColor ChessRules::GetSideToMove() const
{
    return m_SideToMove;
}

const BoardState& ChessRules::GetBoard() const
{
    return m_Board;
}

CastlingRights ChessRules::GetCastlingRights() const
{
    return m_Rights;
}

std::optional<int> ChessRules::GetEnPassantTarget() const
{
    return m_EnPassantTarget;
}

std::optional<int> ChessRules::CheckedKingSquare() const
{
    const std::optional<int> kingSquare = ChessBoardOps::FindKing(m_Board, m_SideToMove);
    if (!kingSquare || !ChessBoardOps::IsSquareAttacked(m_Board, *kingSquare, ChessBoardOps::Opposite(m_SideToMove)))
        return std::nullopt;

    return kingSquare;
}
