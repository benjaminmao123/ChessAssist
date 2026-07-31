#include "BoardStateExtractor.h"
#include "CellBackground.h"

#include <vector>

namespace BoardStateExtractor
{
BoardState Extract(const std::array<cv::Mat, 64>& cells, const PieceTemplateLibrary& library)
{
    // Self-calibrating per-poll reference: aggregate every light (and every dark) cell's own
    // background estimate across the WHOLE board. Only the 2-4 last-moved squares are ever
    // highlighted at a given moment, so the median across the other ~60 cells is a reliable
    // "clean" color for that square color - letting PieceTemplateLibrary correct for
    // whatever highlight tint a given site/theme uses without needing a pre-supplied
    // reference image.
    std::vector<cv::Vec3b> lightSamples;
    std::vector<cv::Vec3b> darkSamples;

    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const cv::Mat& cell = cells[SquareIndex(file, rank)];
            if (cell.empty())
                continue;

            const cv::Vec3b background = CellBackground::EstimateFromWholeCell(cell);
            (IsLightSquare(file, rank) ? lightSamples : darkSamples).push_back(background);
        }
    }

    const cv::Vec3b lightReference = CellBackground::Median(lightSamples);
    const cv::Vec3b darkReference = CellBackground::Median(darkSamples);

    BoardState state{};

    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const int index = SquareIndex(file, rank);
            const cv::Vec3b& reference = IsLightSquare(file, rank) ? lightReference : darkReference;
            state[index] = library.Classify(cells[index], reference);
        }
    }

    return state;
}
}  // namespace BoardStateExtractor
