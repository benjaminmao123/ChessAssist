#include "Engine/UCIProtocol.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

TEST(UCIProtocolTest, BuildPositionCommandWithoutMoves)
{
    EXPECT_EQ(UCIProtocol::BuildPositionCommand("startpos-fen", {}), "position fen startpos-fen");
}

TEST(UCIProtocolTest, BuildPositionCommandWithMoves)
{
    const std::vector<std::string> moves{"e2e4", "e7e5"};
    EXPECT_EQ(UCIProtocol::BuildPositionCommand("fen", moves), "position fen fen moves e2e4 e7e5");
}

TEST(UCIProtocolTest, BuildGoCommandInfiniteIgnoresOtherLimits)
{
    SearchLimits limits;
    limits.Infinite = true;
    limits.Depth = 10;

    EXPECT_EQ(UCIProtocol::BuildGoCommand(limits), "go infinite");
}

TEST(UCIProtocolTest, BuildGoCommandWithDepthAndMoveTime)
{
    SearchLimits limits;
    limits.Depth = 12;
    limits.MoveTimeMs = 1000;

    EXPECT_EQ(UCIProtocol::BuildGoCommand(limits), "go depth 12 movetime 1000");
}

TEST(UCIProtocolTest, ParseInfoLineExtractsDepthScoreAndPv)
{
    const auto info = UCIProtocol::ParseInfoLine("info depth 10 seldepth 15 score cp 34 nodes 12345 nps 500000 time 25 pv e2e4 e7e5");

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->Depth, 10);
    EXPECT_EQ(info->SelDepth, 15);
    EXPECT_EQ(info->ScoreCp, 34);
    EXPECT_FALSE(info->ScoreMate.has_value());
    EXPECT_EQ(info->Nodes, 12345);
    EXPECT_EQ(info->Nps, 500000);
    ASSERT_EQ(info->Pv.size(), 2u);
    EXPECT_EQ(info->Pv[0], "e2e4");
    EXPECT_EQ(info->Pv[1], "e7e5");
}

TEST(UCIProtocolTest, ParseInfoLineDefaultsMultiPvIndexToOneWhenAbsent)
{
    const auto info = UCIProtocol::ParseInfoLine("info depth 10 score cp 34 pv e2e4");

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->MultiPvIndex, 1);
}

TEST(UCIProtocolTest, ParseInfoLineExtractsMultiPvIndex)
{
    const auto info = UCIProtocol::ParseInfoLine("info depth 10 seldepth 15 multipv 2 score cp 12 nodes 500 nps 1000 time 5 pv d2d4 d7d5");

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->MultiPvIndex, 2);
    EXPECT_EQ(info->ScoreCp, 12);
    ASSERT_EQ(info->Pv.size(), 2u);
    EXPECT_EQ(info->Pv[0], "d2d4");
}

TEST(UCIProtocolTest, ParseInfoLineHandlesMateScore)
{
    const auto info = UCIProtocol::ParseInfoLine("info depth 5 score mate 3");

    ASSERT_TRUE(info.has_value());
    EXPECT_EQ(info->ScoreMate, 3);
    EXPECT_FALSE(info->ScoreCp.has_value());
}

TEST(UCIProtocolTest, ParseInfoLineRejectsNonInfoLines)
{
    EXPECT_FALSE(UCIProtocol::ParseInfoLine("bestmove e2e4").has_value());
}

TEST(UCIProtocolTest, ParseBestMoveLineWithoutPonder)
{
    const auto result = UCIProtocol::ParseBestMoveLine("bestmove e2e4");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->BestMove, "e2e4");
    EXPECT_FALSE(result->PonderMove.has_value());
}

TEST(UCIProtocolTest, ParseBestMoveLineWithPonder)
{
    const auto result = UCIProtocol::ParseBestMoveLine("bestmove e2e4 ponder e7e5");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->BestMove, "e2e4");
    ASSERT_TRUE(result->PonderMove.has_value());
    EXPECT_EQ(*result->PonderMove, "e7e5");
}

TEST(UCIProtocolTest, ParseBestMoveLineRejectsNonBestMoveLines)
{
    EXPECT_FALSE(UCIProtocol::ParseBestMoveLine("info depth 1").has_value());
}
