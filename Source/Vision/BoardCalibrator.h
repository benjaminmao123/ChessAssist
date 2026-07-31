#pragma once

#include "VisionTypes.h"

#include <opencv2/core.hpp>

#include <optional>

namespace BoardCalibrator
{
// Opens an interactive window over frame; the user clicks the board's top-left and
// bottom-right corners. Returns nullopt if the user cancels (Esc).
std::optional<BoardRegion> CalibrateInteractive(const cv::Mat& frame, BoardOrientation orientation);

// Cheap heuristic used to detect calibration drift (e.g. the browser window moved or
// was resized): checks whether region still looks like an alternating 8x8 checkerboard.
bool LooksLikeBoard(const cv::Mat& frame, const cv::Rect& region);
}  // namespace BoardCalibrator
