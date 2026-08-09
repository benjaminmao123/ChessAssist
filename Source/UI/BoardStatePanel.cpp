#include "BoardStatePanel.h"

#include "Engine/ExecutablePathUtil.h"
#include "Logging/Log.h"
#include "UI/EngineInfoPanel.h"

#include <imgui.h>

#ifdef _WIN32
// GL/gl.h relies on WINGDIAPI/APIENTRY, which it expects windows.h to have already defined -
// see AppWindow.cpp, which needs the same include order for the same reason.
#define NOMINMAX
#include <windows.h>
#endif

#include <GL/gl.h>

#include <stb_image.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>

namespace
{
// Index into BoardStatePanel::Impl::Textures/kPieceFileNames for a given piece: white pieces
// first (PieceType's own Pawn..King ordering), then black.
int TextureIndex(const Piece& piece)
{
    const int colorOffset = (piece.Color == PieceColor::White) ? 0 : 6;
    return colorOffset + static_cast<int>(piece.Type);
}

constexpr const char* kPieceFileNames[12] = {
    "wP.png", "wN.png", "wB.png", "wR.png", "wQ.png", "wK.png",
    "bP.png", "bN.png", "bB.png", "bR.png", "bQ.png", "bK.png",
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

// Top-left corner of (file, rank)'s square in screen space - canonical board indexing (see
// ChessTypes.h) is always file 0..7 = a..h, rank 0..7 = rank1..8, independent of how it's
// drawn; blackAtBottom flips which screen position that maps to.
ImVec2 SquareMin(int file, int rank, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    const int visualCol = blackAtBottom ? (7 - file) : file;
    const int visualRow = blackAtBottom ? rank : (7 - rank);
    return ImVec2(boardOrigin.x + static_cast<float>(visualCol) * squareSize, boardOrigin.y + static_cast<float>(visualRow) * squareSize);
}

struct SuggestedSquares
{
    ImVec2 FromMin, FromMax, FromCenter;
    ImVec2 ToMin, ToMax, ToCenter;
};

std::optional<SuggestedSquares> ComputeSuggestedSquares(const std::optional<std::string>& suggestedMove, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    if (!suggestedMove || suggestedMove->size() < 4)
        return std::nullopt;

    const std::string& uci = *suggestedMove;
    const int fromFile = uci[0] - 'a';
    const int fromRank = uci[1] - '1';
    const int toFile = uci[2] - 'a';
    const int toRank = uci[3] - '1';

    SuggestedSquares squares;
    squares.FromMin = SquareMin(fromFile, fromRank, blackAtBottom, boardOrigin, squareSize);
    squares.FromMax = ImVec2(squares.FromMin.x + squareSize, squares.FromMin.y + squareSize);
    squares.FromCenter = ImVec2(squares.FromMin.x + squareSize * 0.5f, squares.FromMin.y + squareSize * 0.5f);

    squares.ToMin = SquareMin(toFile, toRank, blackAtBottom, boardOrigin, squareSize);
    squares.ToMax = ImVec2(squares.ToMin.x + squareSize, squares.ToMin.y + squareSize);
    squares.ToCenter = ImVec2(squares.ToMin.x + squareSize * 0.5f, squares.ToMin.y + squareSize * 0.5f);

    return squares;
}

// Line plus a filled triangular head at `to`, pointing from `from` - used to show the
// engine's suggested move as "drag the piece from here to here".
void DrawArrow(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float thickness)
{
    drawList->AddLine(from, to, color, thickness);

    const float dx = to.x - from.x;
    const float dy = to.y - from.y;
    const float length = std::sqrt(dx * dx + dy * dy);
    if (length < 1.0f)
        return;

    const float unitX = dx / length;
    const float unitY = dy / length;
    const float perpX = -unitY;
    const float perpY = unitX;

    constexpr float kHeadLength = 16.0f;
    constexpr float kHeadWidth = 10.0f;

    const ImVec2 baseCenter(to.x - unitX * kHeadLength, to.y - unitY * kHeadLength);
    const ImVec2 left(baseCenter.x + perpX * kHeadWidth, baseCenter.y + perpY * kHeadWidth);
    const ImVec2 right(baseCenter.x - perpX * kHeadWidth, baseCenter.y - perpY * kHeadWidth);

    drawList->AddTriangleFilled(to, left, right, color);
}

// Vertical two-tone bar. Whichever color renders at the bottom of the board (see
// blackAtBottom) also fills from the bottom of the bar here, matching the usual lichess/
// chess.com convention of the bar mirroring the board's own orientation. whiteFraction
// nullopt (no search info yet) draws an even split.
void DrawEvalBar(ImDrawList* drawList, ImVec2 barMin, ImVec2 barMax, std::optional<float> whiteFraction, bool blackAtBottom)
{
    constexpr ImU32 kBlackFillColor = IM_COL32(0x28, 0x28, 0x28, 255);
    constexpr ImU32 kWhiteFillColor = IM_COL32(0xEB, 0xEB, 0xEB, 255);
    constexpr ImU32 kMidlineColor = IM_COL32(0x78, 0x78, 0x78, 180);
    constexpr ImU32 kBorderColor = IM_COL32(0, 0, 0, 255);

    const float whiteFrac = whiteFraction.value_or(0.5f);
    const float bottomFraction = blackAtBottom ? (1.0f - whiteFrac) : whiteFrac;
    const ImU32 bottomColor = blackAtBottom ? kBlackFillColor : kWhiteFillColor;
    const ImU32 topColor = blackAtBottom ? kWhiteFillColor : kBlackFillColor;

    const float barHeight = barMax.y - barMin.y;
    const float bottomHeight = barHeight * bottomFraction;

    drawList->AddRectFilled(barMin, ImVec2(barMax.x, barMax.y - bottomHeight), topColor);
    drawList->AddRectFilled(ImVec2(barMin.x, barMax.y - bottomHeight), barMax, bottomColor);

    const float midY = barMin.y + barHeight * 0.5f;
    drawList->AddLine(ImVec2(barMin.x, midY), ImVec2(barMax.x, midY), kMidlineColor, 1.5f);

    drawList->AddRect(barMin, barMax, kBorderColor);
}
}  // namespace

struct BoardStatePanel::Impl
{
    std::array<GLuint, 12> PieceTextures{};
    GLuint BoardTexture = 0;
    bool TexturesLoaded = false;

    ~Impl()
    {
        if (TexturesLoaded)
        {
            glDeleteTextures(static_cast<GLsizei>(PieceTextures.size()), PieceTextures.data());
            if (BoardTexture != 0)
                glDeleteTextures(1, &BoardTexture);
        }
    }
};

BoardStatePanel::BoardStatePanel(EngineInfoPanel& enginePanel)
    : m_EnginePanel(&enginePanel)
    , m_Impl(std::make_unique<Impl>())
{
}

BoardStatePanel::~BoardStatePanel() = default;

void BoardStatePanel::LoadTextures()
{
    const std::filesystem::path chessAssetsDir = ExecutablePathUtil::GetAssetsDirectory() / "Chess";

    for (std::size_t i = 0; i < m_Impl->PieceTextures.size(); ++i)
    {
        const std::filesystem::path path = chessAssetsDir / kPieceFileNames[i];
        m_Impl->PieceTextures[i] = LoadTexture(path);

        if (m_Impl->PieceTextures[i] == 0)
            LOG_ERROR("BoardStatePanel: failed to load piece texture {}", path.string());
    }

    const std::filesystem::path boardPath = chessAssetsDir / "empty_board.png";
    m_Impl->BoardTexture = LoadTexture(boardPath);
    if (m_Impl->BoardTexture == 0)
        LOG_ERROR("BoardStatePanel: failed to load board texture {} - falling back to a plain checkerboard", boardPath.string());

    m_Impl->TexturesLoaded = true;
}

void BoardStatePanel::Draw(const BoardState& board, bool blackAtBottom, const std::optional<std::string>& suggestedMove, std::optional<float> accuracyPercent)
{
    ImGui::Begin("Tracked Board");

    constexpr float kEvalBarWidth = 28.0f;
    constexpr float kEvalBarGap = 12.0f;
    constexpr float kMinSquareSize = 20.0f;
    constexpr ImU32 kLightSquareColor = IM_COL32(0xEE, 0xEE, 0xD2, 255);
    constexpr ImU32 kDarkSquareColor = IM_COL32(0x76, 0x96, 0x56, 255);
    constexpr ImU32 kSourceHighlightColor = IM_COL32(0xFF, 0xCD, 0x00, 110);
    constexpr ImU32 kDestHighlightColor = IM_COL32(0x00, 0xC8, 0x5A, 110);
    constexpr ImU32 kArrowColor = IM_COL32(0xFF, 0x8C, 0x00, 220);

    // Fills whatever space the panel actually has rather than a fixed pixel size, so the
    // board is as big as the window/dock layout allows instead of floating in a small corner
    // with wasted space around it. reservedForText approximates the height the text below the
    // board needs (Accuracy, then EngineInfoPanel::DrawContents()'s Depth/Score/Nodes/Nps/
    // PV(up to 2 wrapped lines)/separator/Best move) in units that scale with font size/UI
    // scale rather than a hardcoded pixel guess.
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const float reservedForText = ImGui::GetTextLineHeightWithSpacing() * 9.0f;
    const float availableForBoardWidth = available.x - kEvalBarWidth - kEvalBarGap;
    const float availableForBoardHeight = available.y - reservedForText;
    const float squareSize = std::max(std::min(availableForBoardWidth, availableForBoardHeight) / 8.0f, kMinSquareSize);
    const float boardSize = squareSize * 8.0f;

    const ImVec2 panelOrigin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    const ImVec2 barMin = panelOrigin;
    const ImVec2 barMax(panelOrigin.x + kEvalBarWidth, panelOrigin.y + boardSize);
    DrawEvalBar(drawList, barMin, barMax, m_EnginePanel->GetWhiteWinFraction(), blackAtBottom);

    const ImVec2 boardOrigin(panelOrigin.x + kEvalBarWidth + kEvalBarGap, panelOrigin.y);
    const ImVec2 boardEnd(boardOrigin.x + boardSize, boardOrigin.y + boardSize);

    if (m_Impl->BoardTexture != 0)
    {
        drawList->AddImage(static_cast<ImTextureID>(m_Impl->BoardTexture), boardOrigin, boardEnd);
    }
    else
    {
        // Board texture missing/failed to load - draw a plain checkerboard instead so the
        // panel still shows something usable rather than a blank window.
        for (int rank = 0; rank < 8; ++rank)
        {
            for (int file = 0; file < 8; ++file)
            {
                const ImVec2 squareMin = SquareMin(file, rank, blackAtBottom, boardOrigin, squareSize);
                const ImVec2 squareMax(squareMin.x + squareSize, squareMin.y + squareSize);
                drawList->AddRectFilled(squareMin, squareMax, IsLightSquare(file, rank) ? kLightSquareColor : kDarkSquareColor);
            }
        }
    }

    const std::optional<SuggestedSquares> suggested = ComputeSuggestedSquares(suggestedMove, blackAtBottom, boardOrigin, squareSize);
    if (suggested)
    {
        drawList->AddRectFilled(suggested->FromMin, suggested->FromMax, kSourceHighlightColor);
        drawList->AddRectFilled(suggested->ToMin, suggested->ToMax, kDestHighlightColor);
    }

    for (int visualRow = 0; visualRow < 8; ++visualRow)
    {
        for (int visualCol = 0; visualCol < 8; ++visualCol)
        {
            const int file = blackAtBottom ? (7 - visualCol) : visualCol;
            const int rank = blackAtBottom ? visualRow : (7 - visualRow);

            const std::optional<Piece>& piece = board[SquareIndex(file, rank)];
            if (!piece)
                continue;

            const GLuint texture = m_Impl->PieceTextures[TextureIndex(*piece)];
            if (texture == 0)
                continue;

            const ImVec2 squareMin(boardOrigin.x + static_cast<float>(visualCol) * squareSize, boardOrigin.y + static_cast<float>(visualRow) * squareSize);
            const ImVec2 squareMax(squareMin.x + squareSize, squareMin.y + squareSize);
            drawList->AddImage(static_cast<ImTextureID>(texture), squareMin, squareMax);
        }
    }

    // Arrow drawn last so it sits on top of the pieces instead of being hidden under them.
    if (suggested)
        DrawArrow(drawList, suggested->FromCenter, suggested->ToCenter, kArrowColor, std::max(squareSize * 0.1f, 3.0f));

    // Reserves layout space for the eval bar + board so the window sizes/scrolls correctly -
    // everything above was drawn directly via the draw list, not through any layout-owning
    // widget.
    ImGui::Dummy(ImVec2(kEvalBarWidth + kEvalBarGap + boardSize, boardSize));

    if (accuracyPercent)
        ImGui::Text("Accuracy: %.1f%%", *accuracyPercent);
    else
        ImGui::TextDisabled("Accuracy: -");

    ImGui::Separator();
    m_EnginePanel->DrawContents();

    ImGui::End();
}
