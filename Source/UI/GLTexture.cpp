#include "GLTexture.h"

#include <opencv2/imgproc.hpp>

#ifdef _WIN32
// GL/gl.h relies on WINGDIAPI/APIENTRY, which it expects windows.h to have already
// defined - unlike Linux's GL/gl.h, which has no such dependency.
#define NOMINMAX
#include <windows.h>
#endif

#include <GL/gl.h>

GLTexture::GLTexture()
{
    glGenTextures(1, &m_TextureId);
}

GLTexture::~GLTexture()
{
    if (m_TextureId != 0)
        glDeleteTextures(1, &m_TextureId);
}

void GLTexture::Upload(const cv::Mat& frame)
{
    if (frame.empty())
        return;

    m_Width = frame.cols;
    m_Height = frame.rows;

    // OpenCV's native channel order is BGR; OpenGL's base GL_RGB format (unlike GL_BGR,
    // which isn't provided by imgui's bundled loader) expects RGB, so convert once here.
    cv::Mat rgbFrame;
    cv::cvtColor(frame, rgbFrame, cv::COLOR_BGR2RGB);

    glBindTexture(GL_TEXTURE_2D, m_TextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_Width, m_Height, 0, GL_RGB, GL_UNSIGNED_BYTE, rgbFrame.data);
}

ImTextureID GLTexture::Id() const
{
    return static_cast<ImTextureID>(m_TextureId);
}

int GLTexture::Width() const
{
    return m_Width;
}

int GLTexture::Height() const
{
    return m_Height;
}

bool GLTexture::IsValid() const
{
    return m_TextureId != 0 && m_Width > 0 && m_Height > 0;
}
