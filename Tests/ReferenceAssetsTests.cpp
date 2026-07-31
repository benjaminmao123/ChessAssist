#include "Engine/ExecutablePathUtil.h"
#include "Vision/BoardSlicer.h"
#include "Vision/BoardStateExtractor.h"
#include "Vision/PieceTemplateLibrary.h"
#include "Vision/VisionTypes.h"

#include <gtest/gtest.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <array>
#include <filesystem>

namespace
{
// Alpha-blends an RGBA piece sprite onto board at the given cell (WhiteBottom
// orientation), resized to fill most of the cell - mirrors how a real board renders a
// piece sitting on top of its square.
void CompositePieceOntoCell(cv::Mat& board, int file, int rank, const cv::Mat& pieceRgba)
{
    const int cellWidth = board.cols / 8;
    const int cellHeight = board.rows / 8;
    const int col = file;
    const int row = 7 - rank;

    const int targetSize = static_cast<int>(std::min(cellWidth, cellHeight) * 0.75);
    cv::Mat resizedPiece;
    cv::resize(pieceRgba, resizedPiece, cv::Size(targetSize, targetSize));

    const int offsetX = col * cellWidth + (cellWidth - targetSize) / 2;
    const int offsetY = row * cellHeight + (cellHeight - targetSize) / 2;

    std::vector<cv::Mat> channels;
    cv::split(resizedPiece, channels);  // B, G, R, A

    cv::Mat alpha;
    channels[3].convertTo(alpha, CV_32FC1, 1.0 / 255.0);

    cv::Mat roi = board(cv::Rect(offsetX, offsetY, targetSize, targetSize));
    for (int y = 0; y < targetSize; ++y)
    {
        for (int x = 0; x < targetSize; ++x)
        {
            const float a = alpha.at<float>(y, x);
            if (a <= 0.0f)
                continue;

            cv::Vec3b& dst = roi.at<cv::Vec3b>(y, x);
            const cv::Vec4b& src = resizedPiece.at<cv::Vec4b>(y, x);

            for (int c = 0; c < 3; ++c)
                dst[c] = static_cast<uchar>(src[c] * a + dst[c] * (1.0f - a));
        }
    }
}

std::filesystem::path AssetsDir()
{
    return ExecutablePathUtil::GetAssetsDirectory() / "Chessdotcom";
}
}  // namespace

TEST(ReferenceAssetsTest, BootstrapsSuccessfullyFromRealAssets)
{
    PieceTemplateLibrary library;
    ASSERT_TRUE(library.BootstrapFromReferenceAssets(AssetsDir()));
    EXPECT_TRUE(library.IsBootstrapped());
}

TEST(ReferenceAssetsTest, ClassifiesEachRealPieceSpriteCorrectlyWhenCompositedOntoTheRealBoard)
{
    PieceTemplateLibrary library;
    ASSERT_TRUE(library.BootstrapFromReferenceAssets(AssetsDir()));

    const cv::Mat emptyBoard = cv::imread((AssetsDir() / "empty_board.png").string(), cv::IMREAD_COLOR);
    ASSERT_FALSE(emptyBoard.empty());

    const struct
    {
        const char* Filename;
        Piece PieceValue;
        int File;
        int Rank;
    } pieces[] = {
        {"wP.png", Piece{PieceType::Pawn, PieceColor::White}, 0, 1},
        {"bP.png", Piece{PieceType::Pawn, PieceColor::Black}, 0, 6},
        {"wN.png", Piece{PieceType::Knight, PieceColor::White}, 1, 0},
        {"bN.png", Piece{PieceType::Knight, PieceColor::Black}, 1, 7},
        {"wB.png", Piece{PieceType::Bishop, PieceColor::White}, 2, 0},
        {"bB.png", Piece{PieceType::Bishop, PieceColor::Black}, 2, 7},
        {"wR.png", Piece{PieceType::Rook, PieceColor::White}, 3, 0},
        {"bR.png", Piece{PieceType::Rook, PieceColor::Black}, 3, 7},
        {"wQ.png", Piece{PieceType::Queen, PieceColor::White}, 4, 0},
        {"bQ.png", Piece{PieceType::Queen, PieceColor::Black}, 4, 7},
        {"wK.png", Piece{PieceType::King, PieceColor::White}, 5, 0},
        {"bK.png", Piece{PieceType::King, PieceColor::Black}, 5, 7},
    };

    cv::Mat board = emptyBoard.clone();
    for (const auto& piece : pieces)
    {
        const cv::Mat sprite = cv::imread((AssetsDir() / piece.Filename).string(), cv::IMREAD_UNCHANGED);
        ASSERT_FALSE(sprite.empty()) << piece.Filename;
        CompositePieceOntoCell(board, piece.File, piece.Rank, sprite);
    }

    const BoardRegion region{cv::Rect(0, 0, board.cols, board.rows), BoardOrientation::WhiteBottom};
    const std::array<cv::Mat, 64> cells = BoardSlicer::SliceCells(board, region);
    const BoardState state = BoardStateExtractor::Extract(cells, library);

    for (const auto& piece : pieces)
    {
        const std::optional<Piece>& classified = state[SquareIndex(piece.File, piece.Rank)];
        EXPECT_TRUE(classified.has_value()) << piece.Filename << " classified as empty";
        if (classified)
            EXPECT_EQ(*classified, piece.PieceValue) << piece.Filename;
    }

    // Everything else on this board is still empty.
    int emptyMismatches = 0;
    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const bool isOneOfOurPieces = std::any_of(std::begin(pieces), std::end(pieces), [&](const auto& piece) { return piece.File == file && piece.Rank == rank; });

            if (!isOneOfOurPieces && state[SquareIndex(file, rank)].has_value())
                ++emptyMismatches;
        }
    }

    EXPECT_EQ(emptyMismatches, 0);
}


