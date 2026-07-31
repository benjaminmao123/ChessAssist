#pragma once

#include <opencv2/core.hpp>

#include <memory>

class ScreenCapture
{
public:
    ScreenCapture();
    ~ScreenCapture();
    ScreenCapture(const ScreenCapture&) = delete;
    ScreenCapture& operator=(const ScreenCapture&) = delete;

    // Region is in primary-screen pixel coordinates. Returns a BGR image, or an empty Mat
    // if the capture failed.
    [[nodiscard]] cv::Mat CaptureRegion(const cv::Rect& region) const;
    [[nodiscard]] cv::Size GetScreenSize() const;

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
