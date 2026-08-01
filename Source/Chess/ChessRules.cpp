#include "ChessRules.h"

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

PieceColor Opposite(PieceColor color)
{
    return color == PieceColor::White ? PieceColor::Black : PieceColor::White;
}

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

// Pawn pushes/captures/en passant are handled separately from this - pawns are the only
// piece whose reachability depends on direction-of-travel (captures only diagonally, pushes
// only straight) rather than pure geometry.
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
}  // namespace

void ChessRules::Reset()
{
    m_Board = BoardState{};
    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            m_Board[SquareIndex(file, rank)] = GetStandardStartingPiece(file, rank);

    m_SideToMove = PieceColor::White;
    m_EnPassantTarget.reset();
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

    m_Board[kingFrom] = std::nullopt;
    m_Board[rookFrom] = std::nullopt;
    m_Board[kingTo] = king;
    m_Board[rookTo] = rook;

    m_EnPassantTarget.reset();
    m_SideToMove = Opposite(m_SideToMove);

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
            if (!PawnCanReach(m_Board, idx, destIndex, m_SideToMove, parsed->IsCapture, m_EnPassantTarget))
                continue;
        }
        else
        {
            if (m_Board[destIndex] && m_Board[destIndex]->Color == m_SideToMove)
                continue;
            if (!CanPieceReach(m_Board, parsed->Type, idx, destIndex))
                continue;
        }

        candidates.push_back(idx);
    }

    if (candidates.empty())
        return std::nullopt;

    if (candidates.size() > 1)
    {
        // Pure geometric reachability can leave more than one candidate when one of them is
        // actually pinned to its own king (and therefore illegal) - resolve that narrow case
        // by simulating each candidate and keeping only ones that don't leave the mover's own
        // king in check afterward. Scoped to only run on genuine ambiguity, not every move.
        std::vector<int> legal;
        for (int candidate : candidates)
        {
            const bool isEnPassant = parsed->Type == PieceType::Pawn && parsed->IsCapture && !m_Board[destIndex];
            const std::optional<int> epCapture = isEnPassant ? std::optional<int>(SquareIndex(parsed->DestFile, candidate / 8)) : std::nullopt;

            BoardState scratch = m_Board;
            ApplyMoveOnBoard(scratch, candidate, destIndex, parsed->Promotion, epCapture);

            const std::optional<int> kingSquare = FindKing(scratch, m_SideToMove);
            if (kingSquare && !IsSquareAttacked(scratch, *kingSquare, Opposite(m_SideToMove)))
                legal.push_back(candidate);
        }

        if (legal.size() != 1)
            return std::nullopt;

        candidates = legal;
    }

    const int source = candidates.front();

    std::optional<int> enPassantCaptureSquare;
    if (parsed->Type == PieceType::Pawn && parsed->IsCapture && !m_Board[destIndex])
        enPassantCaptureSquare = SquareIndex(parsed->DestFile, source / 8);

    ApplyMoveOnBoard(m_Board, source, destIndex, parsed->Promotion, enPassantCaptureSquare);

    m_EnPassantTarget.reset();
    if (parsed->Type == PieceType::Pawn && std::abs(destIndex / 8 - source / 8) == 2)
        m_EnPassantTarget = SquareIndex(parsed->DestFile, (source / 8 + destIndex / 8) / 2);

    m_SideToMove = Opposite(m_SideToMove);

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
