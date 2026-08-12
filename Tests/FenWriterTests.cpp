#include "Chess/FenWriter.h"

#include "Chess/ChessRules.h"

#include <gtest/gtest.h>

TEST(FenWriterTest, StartingPositionRoundTrips)
{
    ChessRules rules;
    rules.Reset();

    const std::string fen = ToFen(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());
    EXPECT_EQ(fen, std::string(kStandardStartFen));
}

TEST(FenWriterTest, ReflectsSideToMove)
{
    ChessRules rules;
    rules.Reset();
    ASSERT_EQ(rules.ApplySanMove("e4"), "e2e4");

    const std::string fen = ToFen(rules.GetBoard(), rules.GetSideToMove(), rules.GetCastlingRights(), rules.GetEnPassantTarget());

    // Side-to-move field, castling rights, and the e3 en-passant target should all show up.
    EXPECT_NE(fen.find(" b "), std::string::npos);
    EXPECT_NE(fen.find("KQkq"), std::string::npos);
    EXPECT_NE(fen.find(" e3 "), std::string::npos);
}

TEST(FenWriterTest, WritesDashesWhenNoCastlingRightsOrEnPassant)
{
    ChessRules rules;
    rules.Reset();
    ASSERT_EQ(rules.ApplySanMove("Nc3"), "b1c3");  // a quiet non-double-push move

    const std::string fen = ToFen(rules.GetBoard(), rules.GetSideToMove(), CastlingRights{false, false, false, false}, rules.GetEnPassantTarget());

    EXPECT_NE(fen.find(" - -"), std::string::npos);
}
