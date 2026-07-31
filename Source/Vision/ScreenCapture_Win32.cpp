#include "ScreenCapture.h"

#define NOMINMAX
#include <windows.h>

#include <opencv2/imgproc.hpp>

struct ScreenCapture::Impl
{
};

ScreenCapture::ScreenCapture()
    : m_Impl(std::make_unique<Impl>())
{
}

ScreenCapture::~ScreenCapture() = default;

cv::Mat ScreenCapture::CaptureRegion(const cv::Rect& region) const
{
    if (region.width <= 0 || region.height <= 0)
        return {};

    HDC screenDc = GetDC(nullptr);
    if (!screenDc)
        return {};

    HDC memDc = CreateCompatibleDC(screenDc);
    HBITMAP bitmap = CreateCompatibleBitmap(screenDc, region.width, region.height);
    HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);

    // CAPTUREBLT ensures layered/composited windows (e.g. a browser rendering via hardware
    // compositing) are still included in the capture.
    BitBlt(memDc, 0, 0, region.width, region.height, screenDc, region.x, region.y, SRCCOPY | CAPTUREBLT);

    BITMAPINFOHEADER header{};
    header.biSize = sizeof(BITMAPINFOHEADER);
    header.biWidth = region.width;
    header.biHeight = -region.height;  // negative = top-down DIB, matches cv::Mat row order
    header.biPlanes = 1;
    header.biBitCount = 32;
    header.biCompression = BI_RGB;

    cv::Mat frame(region.height, region.width, CV_8UC4);
    GetDIBits(memDc, bitmap, 0, region.height, frame.data, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS);

    SelectObject(memDc, oldBitmap);
    DeleteObject(bitmap);
    DeleteDC(memDc);
    ReleaseDC(nullptr, screenDc);

    cv::Mat bgr;
    cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);
    return bgr;
}

cv::Size ScreenCapture::GetScreenSize() const
{
    return cv::Size(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
}
