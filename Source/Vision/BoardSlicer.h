#pragma once

#include "VisionTypes.h"

#include <opencv2/core.hpp>

#include <array>

namespace BoardSlicer
{
// Crops frame to region and slices it into 64 cells, indexed canonically
// (index = rank * 8 + file, a1 = 0 ... h8 = 63) regardless of on-screen orientation.
std::array<cv::Mat, 64> SliceCells(const cv::Mat& frame, const BoardRegion& region);
}  // namespace BoardSlicer
