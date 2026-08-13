#pragma once

#include "Chess/ChessTypes.h"

#include <array>

// Owns the 12 piece + 1 board-background GL textures, loaded once and shared by every
// interactive chessboard the app draws - loading them per-board would double GPU memory and
// disk I/O for identical PNGs. Owned by App; LoadTextures() called once after the GL context
// exists. Texture handles are exposed as plain unsigned int rather than GLuint so this header
// doesn't need to pull in GL headers itself.
class ChessPieceTextures
{
public:
    ChessPieceTextures() = default;
    ~ChessPieceTextures();
    ChessPieceTextures(const ChessPieceTextures&) = delete;
    ChessPieceTextures& operator=(const ChessPieceTextures&) = delete;

    // Loads the 12 piece PNGs plus the board background from Assets/Chess/ into GL textures.
    // Requires a current GL context, so must be called after AppWindow::Init() succeeds, not
    // from the constructor. Anything that fails to load is logged once here; TextureFor()/
    // BoardTexture() return 0 for it, and callers fall back to a drawn placeholder.
    void LoadTextures();

    // 0 (a never-valid GL texture name) if LoadTextures() hasn't been called yet, or that
    // specific texture failed to load.
    [[nodiscard]] unsigned int TextureFor(const Piece& piece) const;
    [[nodiscard]] unsigned int BoardTexture() const;

private:
    std::array<unsigned int, 12> m_PieceTextures{};
    unsigned int m_BoardTexture = 0;
    bool m_Loaded = false;
};
