#include "FenWriter.h"

#include <cctype>
#include <vector>

namespace
{
std::optional<PieceType> PieceTypeFromFenLetter(char letter)
{
    switch (static_cast<char>(std::tolower(static_cast<unsigned char>(letter))))
    {
    case 'p':
        return PieceType::Pawn;
    case 'n':
        return PieceType::Knight;
    case 'b':
        return PieceType::Bishop;
    case 'r':
        return PieceType::Rook;
    case 'q':
        return PieceType::Queen;
    case 'k':
        return PieceType::King;
    default:
        return std::nullopt;
    }
}

// Splits text on every occurrence of separator, discarding empty pieces. Shared by the
// fields-on-spaces split and the ranks-on-slashes split.
std::vector<std::string_view> Split(std::string_view text, char separator)
{
    std::vector<std::string_view> parts;
    std::size_t pos = 0;
    while (pos < text.size())
    {
        while (pos < text.size() && text[pos] == separator)
            ++pos;
        const std::size_t start = pos;
        while (pos < text.size() && text[pos] != separator)
            ++pos;
        if (pos > start)
            parts.push_back(text.substr(start, pos - start));
    }
    return parts;
}

// Fills board from FEN's piece-placement field (ranks 8 down to 1, separated by '/', each rank
// a mix of digits for empty squares and piece letters). Returns nullopt on malformed input.
std::optional<BoardState> ParsePiecePlacement(std::string_view placement)
{
    const std::vector<std::string_view> ranks = Split(placement, '/');
    if (ranks.size() != 8)
        return std::nullopt;

    BoardState board{};
    for (int rankFromTop = 0; rankFromTop < 8; ++rankFromTop)
    {
        const int rank = 7 - rankFromTop;  // FEN's first rank is rank 8 (this codebase's rank index 7)
        int file = 0;
        for (char c : ranks[rankFromTop])
        {
            if (c >= '1' && c <= '8')
            {
                file += (c - '0');
                continue;
            }

            const std::optional<PieceType> type = PieceTypeFromFenLetter(c);
            if (!type || file >= 8)
                return std::nullopt;

            const PieceColor color = std::isupper(static_cast<unsigned char>(c)) ? PieceColor::White : PieceColor::Black;
            board[SquareIndex(file, rank)] = Piece{*type, color};
            ++file;
        }

        if (file != 8)
            return std::nullopt;
    }

    return board;
}

// True if color has exactly one king on board - a syntactically valid FEN can still describe
// an unanalyzable position, so ParseFen rejects that upfront instead of failing silently later.
bool HasExactlyOneKing(const BoardState& board, PieceColor color)
{
    int count = 0;
    for (const std::optional<Piece>& piece : board)
    {
        if (piece && piece->Type == PieceType::King && piece->Color == color)
            ++count;
    }
    return count == 1;
}

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

std::optional<MoveGenerator::PositionState> ParseFen(std::string_view fen)
{
    const std::vector<std::string_view> fields = Split(fen, ' ');
    if (fields.size() < 4)
        return std::nullopt;

    const std::optional<BoardState> board = ParsePiecePlacement(fields[0]);
    if (!board)
        return std::nullopt;

    PieceColor sideToMove;
    if (fields[1] == "w")
        sideToMove = PieceColor::White;
    else if (fields[1] == "b")
        sideToMove = PieceColor::Black;
    else
        return std::nullopt;

    CastlingRights rights{false, false, false, false};
    if (fields[2] != "-")
    {
        for (char c : fields[2])
        {
            switch (c)
            {
            case 'K':
                rights.WhiteKingside = true;
                break;
            case 'Q':
                rights.WhiteQueenside = true;
                break;
            case 'k':
                rights.BlackKingside = true;
                break;
            case 'q':
                rights.BlackQueenside = true;
                break;
            default:
                return std::nullopt;
            }
        }
    }

    std::optional<int> enPassantTarget;
    if (fields[3] != "-")
    {
        if (fields[3].size() != 2 || fields[3][0] < 'a' || fields[3][0] > 'h' || fields[3][1] < '1' || fields[3][1] > '8')
            return std::nullopt;
        enPassantTarget = SquareIndex(fields[3][0] - 'a', fields[3][1] - '1');
    }

    // Fields[4]/[5] (halfmove clock/fullmove number), if present, are accepted but ignored.

    if (!HasExactlyOneKing(*board, PieceColor::White) || !HasExactlyOneKing(*board, PieceColor::Black))
        return std::nullopt;

    return MoveGenerator::PositionState{*board, sideToMove, rights, enPassantTarget};
}
