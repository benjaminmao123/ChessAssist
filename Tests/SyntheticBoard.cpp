#include "SyntheticBoard.h"

#include <opencv2/imgproc.hpp>

#include <vector>

namespace SyntheticBoard
{
namespace
{
const cv::Scalar kLightBg(210, 210, 210);
const cv::Scalar kDarkBg(90, 90, 90);
const cv::Scalar kWhiteFill(235, 235, 235);
const cv::Scalar kBlackFill(25, 25, 25);
// Contrasts against both backgrounds (diff ~70 vs light, ~50 vs dark) - real piece art
// uses an outline stroke for the same reason: fill color alone can blend into a
// same-colored square (e.g. white fill on a light square) and vanish under a plain
// background-diff threshold.
const cv::Scalar kOutline(140, 140, 140);

void DrawEmptyBoard(cv::Mat& image)
{
    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const cv::Rect rect = CellRect(file, rank);
            cv::rectangle(image, rect, IsLightSquare(file, rank) ? kLightBg : kDarkBg, cv::FILLED);
        }
    }
}
}  // namespace

cv::Rect CellRect(int file, int rank)
{
    return cv::Rect(file * kCellSize, (7 - rank) * kCellSize, kCellSize, kCellSize);
}

void DrawPiece(cv::Mat& image, int file, int rank, const Piece& pieceValue)
{
    const cv::Rect rect = CellRect(file, rank);
    const cv::Point center = (rect.tl() + rect.br()) / 2;
    const int size = static_cast<int>(kCellSize * 0.35);
    const cv::Scalar fill = pieceValue.Color == PieceColor::White ? kWhiteFill : kBlackFill;

    constexpr int kOutlineThickness = 4;

    switch (pieceValue.Type)
    {
    case PieceType::Pawn:
        cv::circle(image, center, size, fill, cv::FILLED);
        cv::circle(image, center, size, kOutline, kOutlineThickness);
        break;

    case PieceType::Knight: {
        const std::vector<cv::Point> triangle{
            {center.x, center.y - size},
            {center.x - size, center.y + size},
            {center.x + size, center.y + size},
        };
        cv::fillConvexPoly(image, triangle, fill);
        cv::polylines(image, std::vector<std::vector<cv::Point>>{triangle}, true, kOutline, kOutlineThickness);
        break;
    }

    case PieceType::Bishop: {
        const std::vector<cv::Point> diamond{
            {center.x, center.y - size},
            {center.x + size, center.y},
            {center.x, center.y + size},
            {center.x - size, center.y},
        };
        cv::fillConvexPoly(image, diamond, fill);
        cv::polylines(image, std::vector<std::vector<cv::Point>>{diamond}, true, kOutline, kOutlineThickness);
        break;
    }

    case PieceType::Rook:
        cv::rectangle(image, cv::Point(center.x - size, center.y - size), cv::Point(center.x + size, center.y + size), fill, cv::FILLED);
        cv::rectangle(image, cv::Point(center.x - size, center.y - size), cv::Point(center.x + size, center.y + size), kOutline, kOutlineThickness);
        break;

    case PieceType::Queen: {
        const int arm = size / 2;
        cv::rectangle(image, cv::Point(center.x - size, center.y - arm), cv::Point(center.x + size, center.y + arm), fill, cv::FILLED);
        cv::rectangle(image, cv::Point(center.x - arm, center.y - size), cv::Point(center.x + arm, center.y + size), fill, cv::FILLED);
        cv::rectangle(image, cv::Point(center.x - size, center.y - arm), cv::Point(center.x + size, center.y + arm), kOutline, kOutlineThickness);
        cv::rectangle(image, cv::Point(center.x - arm, center.y - size), cv::Point(center.x + arm, center.y + size), kOutline, kOutlineThickness);
        break;
    }

    case PieceType::King: {
        const std::vector<cv::Point> triangle{
            {center.x - size, center.y - size},
            {center.x + size, center.y - size},
            {center.x, center.y + size},
        };
        cv::fillConvexPoly(image, triangle, fill);
        cv::polylines(image, std::vector<std::vector<cv::Point>>{triangle}, true, kOutline, kOutlineThickness);
        break;
    }
    }
}

cv::Mat BuildStartingPositionFrame()
{
    cv::Mat frame(kBoardSize, kBoardSize, CV_8UC3);
    DrawEmptyBoard(frame);

    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            if (const std::optional<Piece> startingPiece = GetStandardStartingPiece(file, rank))
                DrawPiece(frame, file, rank, *startingPiece);
        }
    }

    return frame;
}
}  // namespace SyntheticBoard
