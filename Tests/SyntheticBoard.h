#pragma once

#include "Vision/VisionTypes.h"

#include <opencv2/core.hpp>

// Draws synthetic chessboard frames for tests that need to exercise the vision pipeline
// (slicing, template bootstrap, classification) without a real screen capture. Piece
// silhouettes are simple flat shapes with a contrasting outline - not representative of
// real piece-art recognition accuracy, only of the pipeline's data flow.
namespace SyntheticBoard
{
inline constexpr int kCellSize = 100;
inline constexpr int kBoardSize = kCellSize * 8;

// Matches BoardOrientation::WhiteBottom mapping: col = file, row = 7 - rank.
cv::Rect CellRect(int file, int rank);

void DrawPiece(cv::Mat& image, int file, int rank, const Piece& pieceValue);

cv::Mat BuildStartingPositionFrame();
}  // namespace SyntheticBoard
