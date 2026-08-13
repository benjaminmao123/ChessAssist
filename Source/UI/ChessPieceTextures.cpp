#include "ChessPieceTextures.h"

#include "Engine/ExecutablePathUtil.h"
#include "Logging/Log.h"

#ifdef _WIN32
// GL/gl.h relies on WINGDIAPI/APIENTRY, which it expects windows.h to have already defined -
// see AppWindow.cpp, which needs the same include order for the same reason.
#define NOMINMAX
#include <windows.h>
#endif

#if defined(__APPLE__)
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif

#include <stb_image.h>

#include <filesystem>

namespace
{
// Index into ChessPieceTextures::m_PieceTextures/kPieceFileNames for a given piece: white
// pieces first (PieceType's own Pawn..King ordering), then black.
int TextureIndex(const Piece& piece)
{
    const int colorOffset = (piece.Color == PieceColor::White) ? 0 : 6;
    return colorOffset + static_cast<int>(piece.Type);
}

constexpr const char* kPieceFileNames[12] = {
    "wP.png",
    "wN.png",
    "wB.png",
    "wR.png",
    "wQ.png",
    "wK.png",
    "bP.png",
    "bN.png",
    "bB.png",
    "bR.png",
    "bQ.png",
    "bK.png",
};

// Loads path as an RGBA texture and uploads it to a newly-generated GL texture object.
// Returns 0 (a never-valid GL texture name) on failure - stbi_load already logs nothing on
// its own, so the caller is responsible for logging.
GLuint LoadTexture(const std::filesystem::path& path)
{
    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    unsigned char* pixels = stbi_load(path.string().c_str(), &width, &height, &sourceChannels, 4);
    if (!pixels)
        return 0;

    GLuint texture = 0;
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);

    stbi_image_free(pixels);
    return texture;
}
}  // namespace

ChessPieceTextures::~ChessPieceTextures()
{
    if (m_Loaded)
    {
        glDeleteTextures(static_cast<GLsizei>(m_PieceTextures.size()), m_PieceTextures.data());
        if (m_BoardTexture != 0)
            glDeleteTextures(1, &m_BoardTexture);
    }
}

void ChessPieceTextures::LoadTextures()
{
    const std::filesystem::path chessAssetsDir = ExecutablePathUtil::GetAssetsDirectory() / "Chess";

    for (std::size_t i = 0; i < m_PieceTextures.size(); ++i)
    {
        const std::filesystem::path path = chessAssetsDir / kPieceFileNames[i];
        m_PieceTextures[i] = LoadTexture(path);

        if (m_PieceTextures[i] == 0)
            LOG_ERROR("ChessPieceTextures: failed to load piece texture {}", path.string());
    }

    const std::filesystem::path boardPath = chessAssetsDir / "empty_board.png";
    m_BoardTexture = LoadTexture(boardPath);
    if (m_BoardTexture == 0)
        LOG_ERROR("ChessPieceTextures: failed to load board texture {} - falling back to a plain checkerboard", boardPath.string());

    m_Loaded = true;
}

unsigned int ChessPieceTextures::TextureFor(const Piece& piece) const
{
    return m_PieceTextures[TextureIndex(piece)];
}

unsigned int ChessPieceTextures::BoardTexture() const
{
    return m_BoardTexture;
}
