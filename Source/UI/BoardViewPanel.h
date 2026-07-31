#pragma once

#include "GLTexture.h"

#include <opencv2/core.hpp>

// Displays a captured board frame as an ImGui image. Requires an active GL context (i.e.
// must be constructed after AppWindow::Init() succeeds).
class BoardViewPanel
{
public:
    BoardViewPanel() = default;
    BoardViewPanel(const BoardViewPanel&) = delete;
    BoardViewPanel& operator=(const BoardViewPanel&) = delete;

    // Uploads frame to a GPU texture. Not thread-safe: call on the UI/GL thread only.
    void UpdateFrame(const cv::Mat& frame);

    // Not thread-safe: call once per frame from the UI thread only.
    void Draw();

private:
    GLTexture m_Texture;
};
