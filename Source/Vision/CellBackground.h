#pragma once

#include <opencv2/core.hpp>

#include <vector>

// Shared between PieceTemplateLibrary (per-cell occupancy masking and tint correction) and
// BoardStateExtractor (board-wide, self-calibrating tint reference) since both need "what
// color is this cell's background right now".
namespace CellBackground
{
// Per-channel median across every pixel in the cell. The background always covers a clear
// majority of any cell's area - even a large, densely-rendered piece measured well under
// 50% coverage in practice - so the median robustly lands on the background color without
// needing to guess a "safe" sub-region to sample from. That guessing is exactly what broke
// two narrower approaches tried here: a ring sampled right at the cell's edge picks up the
// thin grid line / anti-aliased blend between adjacent squares (confirmed against a real
// capture misreading a corner square's background), while small patches inset near the
// corners instead pick up piece art for wider pieces whose base extends close to the
// corners (confirmed against real rook/bishop/knight captures). The whole-cell median is
// immune to both, since each is too small a minority of the cell's total pixels to move it.
cv::Vec3b EstimateFromWholeCell(const cv::Mat& cell);

// Per-channel median across a set of colors.
cv::Vec3b Median(const std::vector<cv::Vec3b>& colors);
}  // namespace CellBackground
