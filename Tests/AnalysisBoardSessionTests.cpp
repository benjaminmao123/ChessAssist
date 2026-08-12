#include "Game/AnalysisBoardSession.h"

#include "Chess/ChessTypes.h"
#include "Engine/EngineController.h"

#include <gtest/gtest.h>

namespace
{
// AnalysisBoardSession's engine-touching calls (RequestAnalysis -> FindBestMoveAsync) are safe
// against a never-Start()'d EngineController - FindBestMoveAsync immediately resolves with a
// ProcessNotRunning error rather than blocking or crashing (see EngineController::
// FindBestMoveAsync's own !IsRunning() check) - so these tests exercise the real position/
// history/cursor logic through a real object, without spawning an actual Stockfish process.
class AnalysisBoardSessionTest : public ::testing::Test
{
protected:
    EngineController Engine;
    AnalysisBoardSession Session{Engine};
};

std::optional<MoveGenerator::LegalMove> FindMove(const AnalysisBoardSession& session, int from, int to)
{
    for (const MoveGenerator::LegalMove& move : session.GetLegalMovesFrom(from))
    {
        if (move.To == to)
            return move;
    }
    return std::nullopt;
}
}  // namespace

TEST_F(AnalysisBoardSessionTest, StartsAtTheStandardStartingPosition)
{
    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            EXPECT_EQ(Session.GetBoard()[SquareIndex(file, rank)], GetStandardStartingPiece(file, rank));

    EXPECT_EQ(Session.GetSideToMove(), PieceColor::White);
    EXPECT_EQ(Session.HistoryLength(), 0u);
    EXPECT_EQ(Session.GetCursor(), 0u);
    EXPECT_FALSE(Session.CanStepBackward());
    EXPECT_FALSE(Session.CanStepForward());
}

TEST_F(AnalysisBoardSessionTest, PlayMoveAdvancesCursorAndBoard)
{
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());

    Session.PlayMove(*e4);

    EXPECT_EQ(Session.HistoryLength(), 1u);
    EXPECT_EQ(Session.GetCursor(), 1u);
    EXPECT_EQ(Session.GetSideToMove(), PieceColor::Black);
    EXPECT_FALSE(Session.GetBoard()[SquareIndex(4, 1)].has_value());
    ASSERT_TRUE(Session.GetBoard()[SquareIndex(4, 3)].has_value());
    EXPECT_EQ(Session.GetBoard()[SquareIndex(4, 3)]->Type, PieceType::Pawn);
}

TEST_F(AnalysisBoardSessionTest, StepBackwardThenForwardRestoresTheSamePosition)
{
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());
    Session.PlayMove(*e4);

    const BoardState afterE4 = Session.GetBoard();

    Session.StepBackward();
    EXPECT_EQ(Session.GetCursor(), 0u);
    EXPECT_EQ(Session.GetSideToMove(), PieceColor::White);
    EXPECT_TRUE(Session.GetBoard()[SquareIndex(4, 1)].has_value());  // pawn back on e2

    Session.StepForward();
    EXPECT_EQ(Session.GetCursor(), 1u);
    EXPECT_EQ(Session.GetBoard(), afterE4);
}

TEST_F(AnalysisBoardSessionTest, StepBackwardAtStartIsANoOp)
{
    Session.StepBackward();
    EXPECT_EQ(Session.GetCursor(), 0u);
    EXPECT_EQ(Session.GetSideToMove(), PieceColor::White);
}

TEST_F(AnalysisBoardSessionTest, StepForwardAtEndIsANoOp)
{
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());
    Session.PlayMove(*e4);

    Session.StepForward();
    EXPECT_EQ(Session.GetCursor(), 1u);
    EXPECT_EQ(Session.HistoryLength(), 1u);
}

TEST_F(AnalysisBoardSessionTest, PlayingAfterSteppingBackTruncatesTheFuture)
{
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());
    Session.PlayMove(*e4);  // 1. e4

    Session.StepBackward();  // back to the start position

    const std::optional<MoveGenerator::LegalMove> d4 = FindMove(Session, SquareIndex(3, 1), SquareIndex(3, 3));
    ASSERT_TRUE(d4.has_value());
    Session.PlayMove(*d4);  // 1. d4 instead - discards the old "1. e4" future

    EXPECT_EQ(Session.HistoryLength(), 1u);
    EXPECT_EQ(Session.GetCursor(), 1u);
    EXPECT_FALSE(Session.CanStepForward());
    EXPECT_FALSE(Session.GetBoard()[SquareIndex(4, 3)].has_value());  // no pawn on e4
    ASSERT_TRUE(Session.GetBoard()[SquareIndex(3, 3)].has_value());   // pawn on d4 instead
}

TEST_F(AnalysisBoardSessionTest, ResetReturnsToTheStartingPositionAndClearsHistory)
{
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());
    Session.PlayMove(*e4);

    Session.Reset();

    EXPECT_EQ(Session.HistoryLength(), 0u);
    EXPECT_EQ(Session.GetCursor(), 0u);
    EXPECT_EQ(Session.GetSideToMove(), PieceColor::White);
    EXPECT_TRUE(Session.GetBoard()[SquareIndex(4, 1)].has_value());
}

TEST_F(AnalysisBoardSessionTest, FlipBoardTogglesIsBlackAtBottomWithoutChangingThePosition)
{
    EXPECT_FALSE(Session.IsBlackAtBottom());

    const BoardState before = Session.GetBoard();
    Session.FlipBoard();

    EXPECT_TRUE(Session.IsBlackAtBottom());
    EXPECT_EQ(Session.GetBoard(), before);

    Session.FlipBoard();
    EXPECT_FALSE(Session.IsBlackAtBottom());
}

TEST_F(AnalysisBoardSessionTest, LoadFenSetsThePositionAndClearsHistory)
{
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());
    Session.PlayMove(*e4);

    // A simplified king-and-pawn endgame position, Black to move.
    const bool ok = Session.LoadFen("8/8/8/4k3/8/4K3/4P3/8 b - - 0 1");
    ASSERT_TRUE(ok);

    EXPECT_EQ(Session.HistoryLength(), 0u);
    EXPECT_EQ(Session.GetCursor(), 0u);
    EXPECT_EQ(Session.GetSideToMove(), PieceColor::Black);
    ASSERT_TRUE(Session.GetBoard()[SquareIndex(4, 2)].has_value());
    EXPECT_EQ(Session.GetBoard()[SquareIndex(4, 2)]->Type, PieceType::King);
    EXPECT_FALSE(Session.GetBoard()[SquareIndex(3, 1)].has_value());  // d2 pawn from the starting position is gone
}

TEST_F(AnalysisBoardSessionTest, LoadFenRejectsMalformedFenAndLeavesStateUnchanged)
{
    const BoardState before = Session.GetBoard();

    const bool ok = Session.LoadFen("not a fen");

    EXPECT_FALSE(ok);
    EXPECT_EQ(Session.GetBoard(), before);
    EXPECT_EQ(Session.HistoryLength(), 0u);
}

TEST_F(AnalysisBoardSessionTest, ResetAfterLoadFenReturnsToTheLoadedPositionNotTheStandardStart)
{
    ASSERT_TRUE(Session.LoadFen("8/8/8/4k3/8/4K3/4P3/8 b - - 0 1"));
    const BoardState loaded = Session.GetBoard();

    const std::optional<MoveGenerator::LegalMove> kingMove = FindMove(Session, SquareIndex(4, 4), SquareIndex(3, 4));
    ASSERT_TRUE(kingMove.has_value());
    Session.PlayMove(*kingMove);

    Session.Reset();

    EXPECT_EQ(Session.GetBoard(), loaded);
    EXPECT_EQ(Session.HistoryLength(), 0u);
}

TEST_F(AnalysisBoardSessionTest, ResetToStandardStartingPositionDiscardsALoadedFenAndHistory)
{
    ASSERT_TRUE(Session.LoadFen("8/8/8/4k3/8/4K3/4P3/8 b - - 0 1"));
    const std::optional<MoveGenerator::LegalMove> kingMove = FindMove(Session, SquareIndex(4, 4), SquareIndex(3, 4));
    ASSERT_TRUE(kingMove.has_value());
    Session.PlayMove(*kingMove);

    Session.ResetToStandardStartingPosition();

    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            EXPECT_EQ(Session.GetBoard()[SquareIndex(file, rank)], GetStandardStartingPiece(file, rank));

    EXPECT_EQ(Session.GetSideToMove(), PieceColor::White);
    EXPECT_EQ(Session.HistoryLength(), 0u);

    // The starting point itself changed, not just the cursor - a later Reset() must stay at the
    // standard position rather than snapping back to the FEN loaded before
    // ResetToStandardStartingPosition() was called.
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());
    Session.PlayMove(*e4);
    Session.Reset();

    for (int rank = 0; rank < 8; ++rank)
        for (int file = 0; file < 8; ++file)
            EXPECT_EQ(Session.GetBoard()[SquareIndex(file, rank)], GetStandardStartingPiece(file, rank));
}

TEST_F(AnalysisBoardSessionTest, GetFenReturnsTheStandardStartingFenAtTheStart)
{
    EXPECT_EQ(Session.GetFen(), std::string(kStandardStartFen));
}

TEST_F(AnalysisBoardSessionTest, GetFenRoundTripsALoadedFen)
{
    const std::string fen = "8/8/8/4k3/8/4K3/4P3/8 b - - 0 1";
    ASSERT_TRUE(Session.LoadFen(fen));
    EXPECT_EQ(Session.GetFen(), fen);
}

TEST_F(AnalysisBoardSessionTest, GetFenReflectsPlayedMoves)
{
    const std::optional<MoveGenerator::LegalMove> e4 = FindMove(Session, SquareIndex(4, 1), SquareIndex(4, 3));
    ASSERT_TRUE(e4.has_value());
    Session.PlayMove(*e4);

    const std::string fen = Session.GetFen();

    // Side-to-move and the e3 en-passant target should both show up, same assertions as
    // FenWriterTest.ReflectsSideToMove.
    EXPECT_NE(fen.find(" b "), std::string::npos);
    EXPECT_NE(fen.find(" e3 "), std::string::npos);
}
