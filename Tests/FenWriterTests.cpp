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

TEST(FenWriterTest, ParseFenRoundTripsTheStartingPosition)
{
    const std::optional<MoveGenerator::PositionState> parsed = ParseFen(kStandardStartFen);
    ASSERT_TRUE(parsed.has_value());

    EXPECT_EQ(ToFen(parsed->Board, parsed->SideToMove, parsed->Rights, parsed->EnPassantTarget), std::string(kStandardStartFen));
}

TEST(FenWriterTest, ParseFenReadsSideToMoveCastlingAndEnPassant)
{
    const std::optional<MoveGenerator::PositionState> parsed = ParseFen("rnbqkbnr/pppp1ppp/8/4p3/4P3/8/PPPP1PPP/RNBQKBNR w KQkq e6 0 2");
    ASSERT_TRUE(parsed.has_value());

    EXPECT_EQ(parsed->SideToMove, PieceColor::White);
    EXPECT_TRUE(parsed->Rights.WhiteKingside);
    EXPECT_TRUE(parsed->Rights.BlackQueenside);
    ASSERT_TRUE(parsed->EnPassantTarget.has_value());
    EXPECT_EQ(SquareToAlgebraic(*parsed->EnPassantTarget), "e6");
}

TEST(FenWriterTest, ParseFenAcceptsAFenWithoutHalfmoveOrFullmoveFields)
{
    EXPECT_TRUE(ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq -").has_value());
}

TEST(FenWriterTest, ParseFenRejectsTooFewFields)
{
    EXPECT_FALSE(ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq").has_value());
}

TEST(FenWriterTest, ParseFenRejectsWrongRankCount)
{
    EXPECT_FALSE(ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP w KQkq - 0 1").has_value());
}

TEST(FenWriterTest, ParseFenRejectsARankThatDoesNotSumToEightFiles)
{
    EXPECT_FALSE(ParseFen("rnbqkbnr/pppppppp/8/8/8/7/PPPPPPPP/RNBQKBNR w KQkq - 0 1").has_value());
}

TEST(FenWriterTest, ParseFenRejectsAnInvalidPieceLetter)
{
    EXPECT_FALSE(ParseFen("rnbqkxnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").has_value());
}

TEST(FenWriterTest, ParseFenRejectsAnInvalidSideToMove)
{
    EXPECT_FALSE(ParseFen("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR x KQkq - 0 1").has_value());
}

TEST(FenWriterTest, ParseFenRejectsMissingKing)
{
    EXPECT_FALSE(ParseFen("rnbq1bnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").has_value());
}

TEST(FenWriterTest, ParseFenRejectsTwoKingsForOneSide)
{
    EXPECT_FALSE(ParseFen("rnbqkbnr/ppppkppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1").has_value());
}
