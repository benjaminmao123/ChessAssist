#include "Chess/ChessRules.h"
#include "Chess/PolyglotBook.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

namespace
{
struct RawEntry
{
    std::uint64_t Key;
    std::uint16_t Move;
    std::uint16_t Weight;
};

// Mirrors PolyglotBook.cpp's DecodeMove exactly (in reverse) - to/from squares already pack
// as (rank << 3) | file, matching SquareIndex(file, rank).
std::uint16_t EncodeMove(int from, int to, int promotion = 0)
{
    return static_cast<std::uint16_t>((to & 0x3F) | ((from & 0x3F) << 6) | ((promotion & 0x7) << 12));
}

// Writes entries as a real Polyglot .bin file (16 bytes each, big-endian, learn field zeroed)
// to a fresh temp path, for round-tripping through PolyglotBook::Load()/FindMove(). Entries
// need not be pre-sorted - Load() sorts defensively.
std::filesystem::path WriteBookFile(const std::vector<RawEntry>& entries)
{
    static int counter = 0;
    const std::filesystem::path path = std::filesystem::temp_directory_path() / ("polyglot_test_" + std::to_string(++counter) + ".bin");

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    for (const RawEntry& entry : entries)
    {
        for (int b = 7; b >= 0; --b)
            file.put(static_cast<char>((entry.Key >> (b * 8)) & 0xFF));
        file.put(static_cast<char>((entry.Move >> 8) & 0xFF));
        file.put(static_cast<char>(entry.Move & 0xFF));
        file.put(static_cast<char>((entry.Weight >> 8) & 0xFF));
        file.put(static_cast<char>(entry.Weight & 0xFF));
        file.put(0);
        file.put(0);
        file.put(0);
        file.put(0);  // learn - unused
    }

    return path;
}
}  // namespace

// The two official published Polyglot test vectors - the first thing checked, since a single
// wrong constant anywhere in the 781-entry Random64 table (or a wrong indexing/hashing detail)
// would silently break every lookup otherwise. See PolyglotBook.cpp's table comment.
TEST(PolyglotBookTest, HashesStartingPosition)
{
    ChessRules rules;
    rules.Reset();

    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    EXPECT_EQ(hash, 0x463b96181691fc9cULL);
}

TEST(PolyglotBookTest, HashAfterE4ExcludesUncapturableEnPassant)
{
    ChessRules rules;
    rules.Reset();
    ASSERT_TRUE(rules.ApplySanMove("e4").has_value());

    // No Black pawn is adjacent to e4 yet, so the en-passant hash must NOT be included, even
    // though a double push just happened - this exact vector is the standard published check
    // for that rule (a naive "always hash when a double push just happened" implementation
    // produces a different value here).
    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    EXPECT_EQ(hash, 0x823c9b50fd114196ULL);
}

TEST(PolyglotBookTest, HashIncludesReallyCapturableEnPassant)
{
    ChessRules rules;
    rules.Reset();
    ASSERT_TRUE(rules.ApplySanMove("e4").has_value());
    ASSERT_TRUE(rules.ApplySanMove("Nf6").has_value());
    ASSERT_TRUE(rules.ApplySanMove("e5").has_value());
    ASSERT_TRUE(rules.ApplySanMove("d5").has_value());  // White's e5 pawn can now capture en passant

    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    EXPECT_EQ(hash, 0x2158459ff499f8e3ULL);
}

TEST(PolyglotBookTest, FindsPlainMoveForStartingPosition)
{
    ChessRules rules;
    rules.Reset();
    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    const std::filesystem::path path = WriteBookFile({{hash, EncodeMove(SquareIndex(4, 1), SquareIndex(4, 3)), 10}});

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));
    EXPECT_EQ(book.EntryCount(), 1u);

    const std::optional<std::string> move = book.FindMove(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget(), PolyglotBook::SelectionMode::HighestWeight);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e2e4");
}

TEST(PolyglotBookTest, DecodesWhiteKingsideCastlingQuirk)
{
    // Minimal position (not a full legal game) - PolyglotBook only cares about piece placement/
    // rights/EP, not full legality, same scope as ComputeHash itself.
    BoardState board{};
    board[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};
    board[SquareIndex(7, 0)] = Piece{PieceType::Rook, PieceColor::White};
    board[SquareIndex(4, 7)] = Piece{PieceType::King, PieceColor::Black};

    const CastlingRights rights;  // all four true by default
    const std::uint64_t hash = PolyglotBook::ComputeHash(board, PieceColor::White, rights, std::nullopt);

    // Polyglot encodes White O-O as e1h1 (the king "moving to" its own rook's square), not the
    // real two-square e1g1 move.
    const std::filesystem::path path = WriteBookFile({{hash, EncodeMove(SquareIndex(4, 0), SquareIndex(7, 0)), 10}});

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));

    const std::optional<std::string> move = book.FindMove(board, PieceColor::White, rights, std::nullopt, PolyglotBook::SelectionMode::HighestWeight);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e1g1");
}

TEST(PolyglotBookTest, DecodesPromotion)
{
    BoardState board{};
    board[SquareIndex(0, 6)] = Piece{PieceType::Pawn, PieceColor::White};  // a7
    board[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};
    board[SquareIndex(4, 7)] = Piece{PieceType::King, PieceColor::Black};

    const CastlingRights rights{false, false, false, false};
    const std::uint64_t hash = PolyglotBook::ComputeHash(board, PieceColor::White, rights, std::nullopt);
    const std::filesystem::path path = WriteBookFile({{hash, EncodeMove(SquareIndex(0, 6), SquareIndex(0, 7), 4), 10}});  // 4 = queen

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));

    const std::optional<std::string> move = book.FindMove(board, PieceColor::White, rights, std::nullopt, PolyglotBook::SelectionMode::HighestWeight);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "a7a8q");
}

TEST(PolyglotBookTest, HighestWeightModePicksMaxWeightEntry)
{
    ChessRules rules;
    rules.Reset();
    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    const std::filesystem::path path = WriteBookFile({
        {hash, EncodeMove(SquareIndex(4, 1), SquareIndex(4, 3)), 1},    // e2e4, low weight
        {hash, EncodeMove(SquareIndex(3, 1), SquareIndex(3, 3)), 100},  // d2d4, high weight
    });

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));

    for (int i = 0; i < 10; ++i)
    {
        const std::optional<std::string> move = book.FindMove(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget(), PolyglotBook::SelectionMode::HighestWeight);
        ASSERT_TRUE(move.has_value());
        EXPECT_EQ(*move, "d2d4");
    }
}

TEST(PolyglotBookTest, ExcludesZeroWeightEntries)
{
    ChessRules rules;
    rules.Reset();
    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    const std::filesystem::path path = WriteBookFile({
        {hash, EncodeMove(SquareIndex(3, 1), SquareIndex(3, 3)), 0},  // d2d4, soft-deleted (weight 0)
        {hash, EncodeMove(SquareIndex(4, 1), SquareIndex(4, 3)), 5},  // e2e4
    });

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));

    for (const PolyglotBook::SelectionMode mode : {PolyglotBook::SelectionMode::HighestWeight, PolyglotBook::SelectionMode::WeightedRandom})
    {
        const std::optional<std::string> move = book.FindMove(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget(), mode);
        ASSERT_TRUE(move.has_value());
        EXPECT_EQ(*move, "e2e4");
    }
}

TEST(PolyglotBookTest, ReturnsNulloptForUnknownPosition)
{
    ChessRules rules;
    rules.Reset();
    const std::uint64_t startHash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    const std::filesystem::path path = WriteBookFile({{startHash, EncodeMove(SquareIndex(4, 1), SquareIndex(4, 3)), 10}});

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));

    ASSERT_TRUE(rules.ApplySanMove("e4").has_value());  // a different, un-booked position now
    const std::optional<std::string> move = book.FindMove(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget(), PolyglotBook::SelectionMode::HighestWeight);
    EXPECT_FALSE(move.has_value());
}

TEST(PolyglotBookTest, RejectsMoveNotMatchingBoard)
{
    ChessRules rules;
    rules.Reset();
    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());

    // A bogus entry at the starting position's hash claiming a7a5 - White is to move, but a7
    // holds a Black pawn, so this should be rejected rather than handed back.
    const std::filesystem::path path = WriteBookFile({{hash, EncodeMove(SquareIndex(0, 6), SquareIndex(0, 4)), 10}});

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));

    const std::optional<std::string> move = book.FindMove(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget(), PolyglotBook::SelectionMode::HighestWeight);
    EXPECT_FALSE(move.has_value());
}

TEST(PolyglotBookTest, LoadFailsOnTruncatedFile)
{
    const std::filesystem::path path = std::filesystem::temp_directory_path() / "polyglot_test_truncated.bin";
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.put(1);
        file.put(2);
        file.put(3);  // 3 bytes - not a multiple of 16
    }

    PolyglotBook book;
    EXPECT_FALSE(book.Load(path));
    EXPECT_FALSE(book.IsLoaded());
    EXPECT_EQ(book.EntryCount(), 0u);
}

TEST(PolyglotBookTest, ClearEmptiesTheBook)
{
    ChessRules rules;
    rules.Reset();
    const std::uint64_t hash = PolyglotBook::ComputeHash(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    const std::filesystem::path path = WriteBookFile({{hash, EncodeMove(SquareIndex(4, 1), SquareIndex(4, 3)), 10}});

    PolyglotBook book;
    ASSERT_TRUE(book.Load(path));
    ASSERT_TRUE(book.IsLoaded());

    book.Clear();
    EXPECT_FALSE(book.IsLoaded());
    EXPECT_EQ(book.EntryCount(), 0u);
}
