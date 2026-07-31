#pragma once

#include "PieceTemplateLibrary.h"
#include "VisionTypes.h"

#include <opencv2/core.hpp>

#include <array>

namespace BoardStateExtractor
{
BoardState Extract(const std::array<cv::Mat, 64>& cells, const PieceTemplateLibrary& library);
}
