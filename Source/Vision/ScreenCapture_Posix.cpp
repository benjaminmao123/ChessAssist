#include "ScreenCapture.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>

#include <opencv2/imgproc.hpp>

struct ScreenCapture::Impl
{
    Display* DisplayHandle = nullptr;

    Impl()
    {
        DisplayHandle = XOpenDisplay(nullptr);
    }

    ~Impl()
    {
        if (DisplayHandle)
            XCloseDisplay(DisplayHandle);
    }
};

ScreenCapture::ScreenCapture()
    : m_Impl(std::make_unique<Impl>())
{
}

ScreenCapture::~ScreenCapture() = default;

cv::Mat ScreenCapture::CaptureRegion(const cv::Rect& region) const
{
    if (!m_Impl->DisplayHandle || region.width <= 0 || region.height <= 0)
        return {};

    Window root = DefaultRootWindow(m_Impl->DisplayHandle);

    // Assumes a 32bpp BGRA-ordered TrueColor visual, which is the common case on modern
    // desktop Linux X servers.
    XImage* image = XGetImage(m_Impl->DisplayHandle, root, region.x, region.y, region.width, region.height, AllPlanes, ZPixmap);
    if (!image)
        return {};

    cv::Mat frame(region.height, region.width, CV_8UC4, image->data);
    cv::Mat bgr;
    cv::cvtColor(frame, bgr, cv::COLOR_BGRA2BGR);

    XDestroyImage(image);
    return bgr;
}

cv::Size ScreenCapture::GetScreenSize() const
{
    if (!m_Impl->DisplayHandle)
        return {};

    const int screenNum = DefaultScreen(m_Impl->DisplayHandle);
    return cv::Size(DisplayWidth(m_Impl->DisplayHandle, screenNum), DisplayHeight(m_Impl->DisplayHandle, screenNum));
}
