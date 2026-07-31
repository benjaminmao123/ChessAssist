#include "BoardStateExtractor.h"

namespace BoardStateExtractor
{
BoardState Extract(const std::array<cv::Mat, 64>& cells, const PieceTemplateLibrary& library)
{
    BoardState state{};

    for (int rank = 0; rank < 8; ++rank)
    {
        for (int file = 0; file < 8; ++file)
        {
            const int index = SquareIndex(file, rank);
            state[index] = library.Classify(cells[index], IsLightSquare(file, rank));
        }
    }

    return state;
}
}  // namespace BoardStateExtractor
