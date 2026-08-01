#include "Game/MoveListDiff.h"

#include <gtest/gtest.h>

// GameSession's own browser/CDP plumbing needs a real Chrome instance to exercise
// meaningfully (see BrowserPipelineTests.cpp) - same precedent EngineControllerTests already
// set by spawning a real Stockfish rather than mocking. The one piece of GameSession::Poll()
// that's pure decision logic - independent of any browser/engine dependency - is extracted
// into ComputeMoveListDiff() specifically so it can be unit-tested here in isolation.

TEST(MoveListDiffTest, NoChangeWhenCountsMatch)
{
    const MoveListDiff diff = ComputeMoveListDiff(4, 4);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::NoChange);
}

TEST(MoveListDiffTest, NoChangeWhenBothZero)
{
    const MoveListDiff diff = ComputeMoveListDiff(0, 0);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::NoChange);
}

TEST(MoveListDiffTest, GrewReportsCorrectStartIndex)
{
    const MoveListDiff diff = ComputeMoveListDiff(6, 4);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::Grew);
    EXPECT_EQ(diff.StartIndex, 4u);
}

TEST(MoveListDiffTest, GrewFromZeroStartsAtZero)
{
    const MoveListDiff diff = ComputeMoveListDiff(3, 0);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::Grew);
    EXPECT_EQ(diff.StartIndex, 0u);
}

TEST(MoveListDiffTest, ShrinkToZeroIsFreshGameReset)
{
    const MoveListDiff diff = ComputeMoveListDiff(0, 10);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::ResetToFreshGame);
}

TEST(MoveListDiffTest, ShrinkToOneIsFreshGameReset)
{
    const MoveListDiff diff = ComputeMoveListDiff(1, 10);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::ResetToFreshGame);
}

TEST(MoveListDiffTest, ShrinkToTwoOrMoreIsAmbiguous)
{
    const MoveListDiff diff = ComputeMoveListDiff(2, 10);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::AmbiguousShrink);
}

TEST(MoveListDiffTest, ShrinkByOneIsAmbiguous)
{
    const MoveListDiff diff = ComputeMoveListDiff(9, 10);
    EXPECT_EQ(diff.Kind, MoveListDiffKind::AmbiguousShrink);
}
