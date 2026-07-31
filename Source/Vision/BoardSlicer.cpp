#include "BoardSlicer.h"

namespace
{
cv::Point CellPixelPosition(int file, int rank, BoardOrientation orientation)
{
    if (orientation == BoardOrientation::WhiteBottom)
        return cv::Point(file, 7 - rank);

    return cv::Point(7 - file, rank);
}
}  // namespace

namespace BoardSlicer
{
std::array<cv::Mat, 64> SliceCells(const cv::Mat& frame, const BoardRegion& region)
{
    std::array<cv::Mat, 64> cells;

    const cv::Rect bounds = region.Rect & cv::Rect(0, 0, frame.cols, frame.rows);
    if (bounds.width <= 0 || bounds.height <= 0)
        return cells;

    cv::Mat cropped = frame(bounds);

    const double cellWidth = static_cast<double>(cropped.cols) / 8.0;
    const double cellHeight = static_cast<double>(cropped.rows) / 8.0;

    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const cv::Point pixelPosition = CellPixelPosition(file, rank, region.Orientation);

            const cv::Rect cellRect(
                static_cast<int>(pixelPosition.x * cellWidth),
                static_cast<int>(pixelPosition.y * cellHeight),
                static_cast<int>(cellWidth),
                static_cast<int>(cellHeight));

            const cv::Rect clampedRect = cellRect & cv::Rect(0, 0, cropped.cols, cropped.rows);
            if (clampedRect.width > 0 && clampedRect.height > 0)
                cells[SquareIndex(file, rank)] = cropped(clampedRect).clone();
        }
    }

    return cells;
}
}  // namespace BoardSlicer
