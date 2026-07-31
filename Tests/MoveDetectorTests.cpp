#include "Game/MoveDetector.h"
#include "Vision/VisionTypes.h"

#include <gtest/gtest.h>

namespace
{
BoardState MakeStartingBoardState()
{
    BoardState state{};
    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
            state[SquareIndex(file, rank)] = GetStandardStartingPiece(file, rank);
    }
    return state;
}
}  // namespace

TEST(MoveDetectorTest, DetectsSimplePawnPush)
{
    const BoardState before = MakeStartingBoardState();
    BoardState after = before;

    after[SquareIndex(4, 1)] = std::nullopt;                               // e2 empty
    after[SquareIndex(4, 3)] = Piece{PieceType::Pawn, PieceColor::White};  // e4

    const auto move = MoveDetector::DetectMove(before, after, PieceColor::White);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e2e4");
}

TEST(MoveDetectorTest, DetectsCapture)
{
    BoardState before = MakeStartingBoardState();
    // Put a black knight on e4 so the white pawn push below "captures" it.
    before[SquareIndex(4, 3)] = Piece{PieceType::Knight, PieceColor::Black};

    BoardState after = before;
    after[SquareIndex(4, 1)] = std::nullopt;
    after[SquareIndex(4, 3)] = Piece{PieceType::Pawn, PieceColor::White};

    const auto move = MoveDetector::DetectMove(before, after, PieceColor::White);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e2e4");
}

TEST(MoveDetectorTest, DetectsPromotion)
{
    BoardState before{};
    before[SquareIndex(4, 6)] = Piece{PieceType::Pawn, PieceColor::White};  // e7

    BoardState after{};
    after[SquareIndex(4, 7)] = Piece{PieceType::Queen, PieceColor::White};  // e8=Q

    const auto move = MoveDetector::DetectMove(before, after, PieceColor::White);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e7e8q");
}

TEST(MoveDetectorTest, DetectsEnPassant)
{
    BoardState before{};
    before[SquareIndex(4, 4)] = Piece{PieceType::Pawn, PieceColor::White};  // e5
    before[SquareIndex(3, 4)] = Piece{PieceType::Pawn, PieceColor::Black};  // d5, just double-pushed

    BoardState after{};
    after[SquareIndex(3, 5)] = Piece{PieceType::Pawn, PieceColor::White};  // d6 - e5 and d5 both empty now

    const auto move = MoveDetector::DetectMove(before, after, PieceColor::White);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e5d6");
}

TEST(MoveDetectorTest, DetectsWhiteKingsideCastling)
{
    BoardState before{};
    before[SquareIndex(4, 0)] = Piece{PieceType::King, PieceColor::White};  // e1
    before[SquareIndex(7, 0)] = Piece{PieceType::Rook, PieceColor::White};  // h1

    BoardState after{};
    after[SquareIndex(6, 0)] = Piece{PieceType::King, PieceColor::White};  // g1
    after[SquareIndex(5, 0)] = Piece{PieceType::Rook, PieceColor::White};  // f1

    const auto move = MoveDetector::DetectMove(before, after, PieceColor::White);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e1g1");
}

TEST(MoveDetectorTest, DetectsBlackQueensideCastling)
{
    BoardState before{};
    before[SquareIndex(4, 7)] = Piece{PieceType::King, PieceColor::Black};  // e8
    before[SquareIndex(0, 7)] = Piece{PieceType::Rook, PieceColor::Black};  // a8

    BoardState after{};
    after[SquareIndex(2, 7)] = Piece{PieceType::King, PieceColor::Black};  // c8
    after[SquareIndex(3, 7)] = Piece{PieceType::Rook, PieceColor::Black};  // d8

    const auto move = MoveDetector::DetectMove(before, after, PieceColor::Black);
    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e8c8");
}

TEST(MoveDetectorTest, ReturnsNulloptForUnrecognizedDiffShape)
{
    const BoardState before = MakeStartingBoardState();
    BoardState after = before;
    after[SquareIndex(4, 1)] = std::nullopt;  // only one square changed - not a valid move shape

    EXPECT_FALSE(MoveDetector::DetectMove(before, after, PieceColor::White).has_value());
}

TEST(MoveDetectorTest, ReturnsNulloptWhenNothingChanged)
{
    const BoardState before = MakeStartingBoardState();
    const BoardState after = before;

    EXPECT_FALSE(MoveDetector::DetectMove(before, after, PieceColor::White).has_value());
}
