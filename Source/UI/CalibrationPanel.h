#pragma once

#include "GLTexture.h"

#include "../Vision/VisionTypes.h"

#include <opencv2/core.hpp>

#include <optional>
#include <vector>

// ImGui-native replacement for BoardCalibrator's old separate OpenCV window: displays a
// captured frame inside the main app window and lets the user click the board's top-left
// and bottom-right corners directly on it.
class CalibrationPanel
{
public:
    // Begins a new calibration attempt against frame - resets any previous click state.
    void Begin(const cv::Mat& frame, BoardOrientation orientation);

    [[nodiscard]] bool IsActive() const;

    // Not thread-safe: call once per frame from the UI thread only. No-ops if no
    // calibration is in progress.
    void Draw();

    // Returns the completed region once the user has clicked both corners, clearing it.
    // Returns nullopt otherwise (still in progress, or nothing new since the last call).
    [[nodiscard]] std::optional<BoardRegion> TakeResult();

private:
    GLTexture m_Texture;
    BoardOrientation m_Orientation = BoardOrientation::WhiteBottom;
    std::vector<cv::Point> m_Clicks;
    bool m_Active = false;
    std::optional<BoardRegion> m_Result;
};
