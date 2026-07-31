#include "SyntheticBoard.h"

#include "Game/MoveDetector.h"
#include "Vision/BoardSlicer.h"
#include "Vision/BoardStateExtractor.h"
#include "Vision/PieceTemplateLibrary.h"
#include "Vision/VisionTypes.h"

#include <gtest/gtest.h>

#include <opencv2/imgproc.hpp>

namespace
{
struct VisionPipelineFixture : ::testing::Test
{
    cv::Mat StartFrame = SyntheticBoard::BuildStartingPositionFrame();
    BoardRegion Region{cv::Rect(0, 0, SyntheticBoard::kBoardSize, SyntheticBoard::kBoardSize), BoardOrientation::WhiteBottom};
    std::array<cv::Mat, 64> StartCells = BoardSlicer::SliceCells(StartFrame, Region);
    PieceTemplateLibrary Library;

    VisionPipelineFixture()
    {
        Library.BootstrapFromStartingPosition(StartCells);
    }
};
}  // namespace

TEST_F(VisionPipelineFixture, BootstrapsTemplateLibraryFromStartingPosition)
{
    EXPECT_TRUE(Library.IsBootstrapped());
}

TEST_F(VisionPipelineFixture, RoundTripClassificationIsReasonablyAccurate)
{
    const BoardState state = BoardStateExtractor::Extract(StartCells, Library);

    int mismatches = 0;
    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            if (state[SquareIndex(file, rank)] != GetStandardStartingPiece(file, rank))
                ++mismatches;
        }
    }

    // The synthetic piece shapes (plain circle/triangle/diamond/square/cross) are cruder
    // and more mutually confusable than real piece art, so this is a generous regression
    // guard rather than a claim of production-grade classification accuracy.
    EXPECT_LE(mismatches, 10) << mismatches << " / 64 squares misclassified on round-trip";
}

TEST_F(VisionPipelineFixture, DetectsE2E4Advance)
{
    cv::Mat nextFrame = StartFrame.clone();

    // Move the e2 pawn to e4: e2 = (file 4, rank 1), e4 = (file 4, rank 3).
    cv::rectangle(nextFrame, SyntheticBoard::CellRect(4, 1), IsLightSquare(4, 1) ? cv::Scalar(210, 210, 210) : cv::Scalar(90, 90, 90), cv::FILLED);
    SyntheticBoard::DrawPiece(nextFrame, 4, 3, Piece{PieceType::Pawn, PieceColor::White});

    const BoardState beforeState = BoardStateExtractor::Extract(StartCells, Library);

    const std::array<cv::Mat, 64> nextCells = BoardSlicer::SliceCells(nextFrame, Region);
    const BoardState afterState = BoardStateExtractor::Extract(nextCells, Library);

    const std::optional<std::string> move = MoveDetector::DetectMove(beforeState, afterState, PieceColor::White);

    ASSERT_TRUE(move.has_value());
    EXPECT_EQ(*move, "e2e4");
}
