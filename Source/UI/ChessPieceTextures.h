#pragma once

#include "Chess/ChessTypes.h"

#include <array>

// Owns the 12 piece + 1 board-background GL textures, loaded once and shared by every
// interactive chessboard the app draws (the live board, the free-standing analysis board) -
// loading them per-board would double GPU memory and disk I/O for identical PNGs. Owned by App,
// LoadTextures() called once after the GL context exists, passed by const& to each board's
// panel (see ChessBoardWidget). Texture handles are exposed as plain unsigned int rather than
// GLuint so this header doesn't need to pull in GL headers itself - the .cpp does, since it's
// the one actually calling glGenTextures/glDeleteTextures.
class ChessPieceTextures
{
public:
    ChessPieceTextures() = default;
    ~ChessPieceTextures();
    ChessPieceTextures(const ChessPieceTextures&) = delete;
    ChessPieceTextures& operator=(const ChessPieceTextures&) = delete;

    // Loads the 12 piece PNGs (wP/wN/wB/wR/wQ/wK/bP/bN/bB/bR/bQ/bK.png) plus the board
    // background (empty_board.png) from Assets/Chess/ into GL textures. Requires a current GL
    // context, so must be called after AppWindow::Init() succeeds, not from the constructor,
    // which runs before the window/context exist. Anything missing or that fails to load is
    // logged once here; TextureFor()/BoardTexture() return 0 for it, and callers (see
    // ChessBoardWidget) fall back to a plain drawn placeholder rather than failing outright.
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
