#pragma once

#include <opencv2/core.hpp>

#include <imgui.h>

// Minimal RAII wrapper around a single GL texture, uploaded from an OpenCV frame each time
// it changes. Must be constructed/used on the UI/GL thread only (i.e. after
// AppWindow::Init() succeeds) since it touches GL state directly.
class GLTexture
{
public:
    GLTexture();
    ~GLTexture();
    GLTexture(const GLTexture&) = delete;
    GLTexture& operator=(const GLTexture&) = delete;

    void Upload(const cv::Mat& frame);

    [[nodiscard]] ImTextureID Id() const;
    [[nodiscard]] int Width() const;
    [[nodiscard]] int Height() const;
    [[nodiscard]] bool IsValid() const;

private:
    unsigned int m_TextureId = 0;
    int m_Width = 0;
    int m_Height = 0;
};
