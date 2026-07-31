#pragma once

#include <opencv2/core.hpp>

namespace BoardCalibrator
{
// Cheap heuristic used to detect calibration drift (e.g. the browser window moved or
// was resized): checks whether region still looks like an alternating 8x8 checkerboard.
bool LooksLikeBoard(const cv::Mat& frame, const cv::Rect& region);
}  // namespace BoardCalibrator
