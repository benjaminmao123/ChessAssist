#include "Chess/ChessRules.h"

#include <gtest/gtest.h>

TEST(ChessRulesTest, ResetGivesStandardStartingPosition)
{
    ChessRules rules;
    rules.Reset();

    const BoardState& board = rules.GetBoard();
    EXPECT_EQ(board[SquareIndex(4, 0)], (Piece{PieceType::King, PieceColor::White}));
    EXPECT_EQ(board[SquareIndex(4, 7)], (Piece{PieceType::King, PieceColor::Black}));
    EXPECT_EQ(board[SquareIndex(0, 1)], (Piece{PieceType::Pawn, PieceColor::White}));
    EXPECT_FALSE(board[SquareIndex(0, 3)].has_value());
    EXPECT_EQ(rules.GetSideToMove(), PieceColor::White);
}

TEST(ChessRulesTest, TracksSideToMove)
{
    ChessRules rules;
    rules.Reset();

    EXPECT_EQ(rules.GetSideToMove(), PieceColor::White);
    ASSERT_TRUE(rules.ApplySanMove("e4").has_value());
    EXPECT_EQ(rules.GetSideToMove(), PieceColor::Black);
    ASSERT_TRUE(rules.ApplySanMove("e5").has_value());
    EXPECT_EQ(rules.GetSideToMove(), PieceColor::White);
}

TEST(ChessRulesTest, AppliesBasicPawnPush)
{
    ChessRules rules;
    rules.Reset();

    const auto move = rules.ApplySanMove("e4");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e2e4");
}

TEST(ChessRulesTest, AppliesBasicKnightMove)
{
    ChessRules rules;
    rules.Reset();

    const auto move = rules.ApplySanMove("Nf3");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "g1f3");
}

TEST(ChessRulesTest, AppliesPawnCapture)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("d5"), "d7d5");

    const auto move = rules.ApplySanMove("exd5");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e4d5");
}

TEST(ChessRulesTest, StripsCheckAndMateSuffixes)
{
    ChessRules rules;
    rules.Reset();

    const auto move = rules.ApplySanMove("Nf3+");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "g1f3");
}

TEST(ChessRulesTest, DisambiguatesByFileWhenTwoKnightsShareATarget)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("Nf3"), "g1f3");
    ASSERT_EQ(rules.ApplySanMove("Nc6"), "b8c6");
    ASSERT_EQ(rules.ApplySanMove("Ng5"), "f3g5");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");
    ASSERT_EQ(rules.ApplySanMove("Ne4"), "g5e4");
    ASSERT_EQ(rules.ApplySanMove("a5"), "a6a5");

    // Both the untouched b1 knight and the e4 knight can reach c3 - needs a file hint since
    // they're on different files.
    const auto move = rules.ApplySanMove("Nbc3");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "b1c3");
}

TEST(ChessRulesTest, DisambiguatesByRankWhenTwoKnightsShareAFile)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("Nf3"), "g1f3");
    ASSERT_EQ(rules.ApplySanMove("Nc6"), "b8c6");
    ASSERT_EQ(rules.ApplySanMove("Nd4"), "f3d4");
    ASSERT_EQ(rules.ApplySanMove("d6"), "d7d6");
    ASSERT_EQ(rules.ApplySanMove("Nb5"), "d4b5");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");

    // The untouched b1 knight and the b5 knight share a file (b) - needs a rank hint.
    const auto move = rules.ApplySanMove("N1c3");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "b1c3");
}

TEST(ChessRulesTest, HandlesEnPassantCapture)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e4e5");
    ASSERT_EQ(rules.ApplySanMove("d5"), "d7d5");

    const auto move = rules.ApplySanMove("exd6");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e5d6");

    // The captured pawn sat on d5, not the (empty) destination square d6.
    EXPECT_FALSE(rules.GetBoard()[SquareIndex(3, 4)].has_value());
}

TEST(ChessRulesTest, HandlesPromotionByCapture)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("d5"), "d7d5");
    ASSERT_EQ(rules.ApplySanMove("exd5"), "e4d5");
    ASSERT_EQ(rules.ApplySanMove("Nc6"), "b8c6");
    ASSERT_EQ(rules.ApplySanMove("dxc6"), "d5c6");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");
    ASSERT_EQ(rules.ApplySanMove("cxb7"), "c6b7");
    ASSERT_EQ(rules.ApplySanMove("a5"), "a6a5");

    const auto move = rules.ApplySanMove("bxa8=Q");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "b7a8q");
    EXPECT_EQ(rules.GetBoard()[SquareIndex(0, 7)], (Piece{PieceType::Queen, PieceColor::White}));
}

TEST(ChessRulesTest, HandlesUnderpromotionToKnight)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("h4"), "h2h4");
    ASSERT_EQ(rules.ApplySanMove("g5"), "g7g5");
    ASSERT_EQ(rules.ApplySanMove("hxg5"), "h4g5");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");
    ASSERT_EQ(rules.ApplySanMove("g6"), "g5g6");
    ASSERT_EQ(rules.ApplySanMove("a5"), "a6a5");
    ASSERT_EQ(rules.ApplySanMove("gxh7"), "g6h7");
    ASSERT_EQ(rules.ApplySanMove("a4"), "a5a4");

    const auto move = rules.ApplySanMove("hxg8=N");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "h7g8n");
    EXPECT_EQ(rules.GetBoard()[SquareIndex(6, 7)], (Piece{PieceType::Knight, PieceColor::White}));
}

TEST(ChessRulesTest, HandlesWhiteKingsideCastle)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e7e5");
    ASSERT_EQ(rules.ApplySanMove("Nf3"), "g1f3");
    ASSERT_EQ(rules.ApplySanMove("Nc6"), "b8c6");
    ASSERT_EQ(rules.ApplySanMove("Bc4"), "f1c4");
    ASSERT_EQ(rules.ApplySanMove("Bc5"), "f8c5");

    const auto move = rules.ApplySanMove("O-O");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e1g1");
    EXPECT_EQ(rules.GetBoard()[SquareIndex(5, 0)], (Piece{PieceType::Rook, PieceColor::White}));  // f1
}

TEST(ChessRulesTest, HandlesWhiteQueensideCastle)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("Nc3"), "b1c3");
    ASSERT_EQ(rules.ApplySanMove("Nc6"), "b8c6");
    ASSERT_EQ(rules.ApplySanMove("d4"), "d2d4");
    ASSERT_EQ(rules.ApplySanMove("d6"), "d7d6");
    ASSERT_EQ(rules.ApplySanMove("Qd3"), "d1d3");
    ASSERT_EQ(rules.ApplySanMove("g6"), "g7g6");
    ASSERT_EQ(rules.ApplySanMove("Be3"), "c1e3");
    ASSERT_EQ(rules.ApplySanMove("Bg7"), "f8g7");

    const auto move = rules.ApplySanMove("O-O-O");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e1c1");
    EXPECT_EQ(rules.GetBoard()[SquareIndex(3, 0)], (Piece{PieceType::Rook, PieceColor::White}));  // d1
}

TEST(ChessRulesTest, HandlesBlackKingsideCastle)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("a4"), "a2a4");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e7e5");
    ASSERT_EQ(rules.ApplySanMove("a5"), "a4a5");
    ASSERT_EQ(rules.ApplySanMove("Nf6"), "g8f6");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a5a6");
    ASSERT_EQ(rules.ApplySanMove("Bc5"), "f8c5");
    ASSERT_EQ(rules.ApplySanMove("h3"), "h2h3");

    const auto move = rules.ApplySanMove("O-O");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e8g8");
}

TEST(ChessRulesTest, HandlesBlackQueensideCastle)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("a4"), "a2a4");
    ASSERT_EQ(rules.ApplySanMove("Nc6"), "b8c6");
    ASSERT_EQ(rules.ApplySanMove("a5"), "a4a5");
    ASSERT_EQ(rules.ApplySanMove("d5"), "d7d5");
    ASSERT_EQ(rules.ApplySanMove("a6"), "a5a6");
    ASSERT_EQ(rules.ApplySanMove("Qd6"), "d8d6");
    ASSERT_EQ(rules.ApplySanMove("h3"), "h2h3");
    ASSERT_EQ(rules.ApplySanMove("Bf5"), "c8f5");
    ASSERT_EQ(rules.ApplySanMove("h4"), "h3h4");

    const auto move = rules.ApplySanMove("O-O-O");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e8c8");
}

TEST(ChessRulesTest, PinTieBreakExcludesPinnedPieceFromAmbiguousMove)
{
    ChessRules rules;
    rules.Reset();

    ASSERT_EQ(rules.ApplySanMove("d4"), "d2d4");
    ASSERT_EQ(rules.ApplySanMove("e5"), "e7e5");
    ASSERT_EQ(rules.ApplySanMove("Nc3"), "b1c3");
    ASSERT_EQ(rules.ApplySanMove("Bb4"), "f8b4");  // pins the c3 knight to White's king on e1
    ASSERT_EQ(rules.ApplySanMove("e3"), "e2e3");   // vacate e2 so a knight can land there
    ASSERT_EQ(rules.ApplySanMove("a6"), "a7a6");

    // Both the pinned c3 knight and the untouched g1 knight can geometrically reach e2, but
    // moving the c3 knight would expose White's king to the b4 bishop - only g1's is legal.
    const auto move = rules.ApplySanMove("Ne2");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "g1e2");
}

TEST(ChessRulesTest, RejectsIllegalMoveAndLeavesStateUnchanged)
{
    ChessRules rules;
    rules.Reset();

    EXPECT_FALSE(rules.ApplySanMove("Nf6").has_value());  // no White knight can reach f6 from start
    EXPECT_EQ(rules.GetSideToMove(), PieceColor::White);  // state unchanged

    const auto move = rules.ApplySanMove("Nf3");
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "g1f3");
}

TEST(ChessRulesTest, RejectsMalformedSan)
{
    ChessRules rules;
    rules.Reset();

    EXPECT_FALSE(rules.ApplySanMove("").has_value());
    EXPECT_FALSE(rules.ApplySanMove("zz9").has_value());
    EXPECT_FALSE(rules.ApplySanMove("K9").has_value());
}

TEST(ChessRulesTest, ReplaysAShortRealGame)
{
    ChessRules rules;
    rules.Reset();

    const struct
    {
        const char* San;
        const char* ExpectedUci;
    } moves[] = {
        {"e4", "e2e4"},
        {"e5", "e7e5"},
        {"Nf3", "g1f3"},
        {"Nc6", "b8c6"},
        {"Bc4", "f1c4"},
        {"Bc5", "f8c5"},
        {"O-O", "e1g1"},
    };

    for (const auto& entry : moves)
    {
        const auto move = rules.ApplySanMove(entry.San);
        ASSERT_TRUE(move.has_value()) << entry.San;
        EXPECT_EQ(*move, entry.ExpectedUci) << entry.San;
    }
}
