#include "BoardStatePanel.h"

#include "Chess/MoveGenerator.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/SandboxSession.h"
#include "ImguiUtils.h"
#include "Logging/Log.h"
#include "Settings/SettingsIni.h"
#include "UI/EngineInfoPanel.h"

#include <imgui.h>
#include <ini/ini.h>

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
#include <cstdio>
#include <filesystem>
#include <iterator>
#include <utility>
#include <vector>

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

// Top-left corner of (file, rank)'s square in screen space - canonical board indexing (see
// ChessTypes.h) is always file 0..7 = a..h, rank 0..7 = rank1..8, independent of how it's
// drawn; blackAtBottom flips which screen position that maps to.
ImVec2 SquareMin(int file, int rank, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    const int visualCol = blackAtBottom ? (7 - file) : file;
    const int visualRow = blackAtBottom ? rank : (7 - rank);
    return ImVec2(boardOrigin.x + static_cast<float>(visualCol) * squareSize, boardOrigin.y + static_cast<float>(visualRow) * squareSize);
}

// Inverse of SquareMin(): the canonically-indexed square under a screen-space point, or
// nullopt if the point falls outside the 8x8 board area.
std::optional<int> SquareAtScreenPos(ImVec2 pos, ImVec2 boardOrigin, float squareSize, bool blackAtBottom)
{
    const float relX = pos.x - boardOrigin.x;
    const float relY = pos.y - boardOrigin.y;
    const float boardSize = squareSize * 8.0f;
    if (relX < 0.0f || relY < 0.0f || relX >= boardSize || relY >= boardSize)
        return std::nullopt;

    const int visualCol = static_cast<int>(relX / squareSize);
    const int visualRow = static_cast<int>(relY / squareSize);

    const int file = blackAtBottom ? (7 - visualCol) : visualCol;
    const int rank = blackAtBottom ? visualRow : (7 - visualRow);
    return SquareIndex(file, rank);
}

// Center point of a canonically-indexed square in screen space - used for user-drawn
// annotation arrows, which (unlike the engine's suggested-move arrow) have no UCI move string
// to derive endpoints from.
ImVec2 SquareCenter(int square, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    const ImVec2 min = SquareMin(square % 8, square / 8, blackAtBottom, boardOrigin, squareSize);
    return ImVec2(min.x + squareSize * 0.5f, min.y + squareSize * 0.5f);
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

// Line plus a filled triangular head at `to`, pointing from `from` - used to show a suggested
// move as "drag the piece from here to here".
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

// Layout/color constants for Draw() and the free/member functions below it - file-scope rather
// than Draw()-local since several of them (board colors, highlight colors) are now shared with
// DrawBoardBackground()/BoardStatePanel::Impl's own drawing methods, not just Draw() itself.
constexpr float kEvalBarWidth = 28.0f;
constexpr float kEvalBarGap = 12.0f;
constexpr float kMinSquareSize = 20.0f;
constexpr ImU32 kLightSquareColor = IM_COL32(0xEE, 0xEE, 0xD2, 255);
constexpr ImU32 kDarkSquareColor = IM_COL32(0x76, 0x96, 0x56, 255);
constexpr ImU32 kSourceHighlightColor = IM_COL32(0xFF, 0xCD, 0x00, 110);
constexpr ImU32 kDestHighlightColor = IM_COL32(0x00, 0xC8, 0x5A, 110);
constexpr ImU32 kArrowColor = IM_COL32(0xFF, 0x8C, 0x00, 220);
constexpr ImU32 kLookaheadArrowColor = IM_COL32(0x3A, 0x8C, 0xFF, 200);
constexpr ImU32 kMatingArrowColor = IM_COL32(0xFF, 0x2A, 0x2A, 235);
constexpr ImU32 kCheckHighlightColor = IM_COL32(0xFF, 0x1A, 0x1A, 150);
constexpr ImU32 kMateBannerBg = IM_COL32(0x20, 0x00, 0x00, 210);
constexpr ImU32 kMateBannerText = IM_COL32(0xFF, 0x70, 0x70, 255);
constexpr ImU32 kPickedSquareColor = IM_COL32(0xFF, 0xFF, 0x00, 90);
constexpr ImU32 kLegalDestinationColor = IM_COL32(0x00, 0x00, 0x00, 140);

// One color per alternate candidate move (see GameSession::GetAlternateMoves()), in order -
// deliberately distinct from the primary (orange/red) and lookahead (blue) arrow colors,
// and deliberately much more transparent than the primary arrow's alpha 220 - low enough
// that the primary (best) move still reads as the obvious, solid one at a glance, with each
// further alternate fading a bit more to reinforce "less preferred than the last." More
// entries than GameSession::kMultiPvLines - 1 ever needs today, so raising that constant
// doesn't immediately run out of colors; alternates beyond the array's length reuse its
// last entry rather than indexing out of bounds.
constexpr ImU32 kAlternateArrowColors[] = {
    IM_COL32(0xC9, 0xA8, 0x00, 130),  // 2nd choice - gold
    IM_COL32(0x9C, 0x27, 0xB0, 100),  // 3rd choice - purple
    IM_COL32(0x00, 0xAC, 0xC1, 75),   // 4th choice - teal
};

// Draws the board image if it loaded, else falls back to a plain drawn checkerboard so the
// panel still shows something usable rather than a blank window.
void DrawBoardBackground(ImDrawList* drawList, GLuint boardTexture, ImVec2 boardOrigin, ImVec2 boardEnd, float squareSize, bool blackAtBottom)
{
    if (boardTexture != 0)
    {
        drawList->AddImage(static_cast<ImTextureID>(boardTexture), boardOrigin, boardEnd);
        return;
    }

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
}  // namespace

struct BoardStatePanel::Impl
{
    std::array<GLuint, 12> PieceTextures{};
    GLuint BoardTexture = 0;
    bool TexturesLoaded = false;

    // Sandbox mouse-interaction state - UI-thread-only, like everything else touching ImGui.
    struct DragState
    {
        enum class Mode
        {
            Idle,
            PickedUp,
            Dragging
        };
        Mode CurrentMode = Mode::Idle;
        int PickedSquare = -1;
        std::vector<MoveGenerator::LegalMove> LegalDestinations;
    };
    DragState Drag;

    // Set when a resolved drop matches more than one legal move (the 4 promotion-choice
    // entries) - a popup is opened instead of playing immediately. Cleared once a choice is
    // made or the popup is dismissed.
    std::optional<std::vector<MoveGenerator::LegalMove>> PendingPromotionChoices;
    int PendingPromotionSquare = -1;

    // User-drawn planning arrows (right-click-drag, lichess-style) - purely a local annotation
    // overlay, unrelated to the sandbox's actual move-playing. Toggled on/off per (from, to)
    // pair by dragging the same arrow again; cleared automatically whenever the drawn board
    // itself changes (see Draw()'s LastSeenBoard check) rather than needing an explicit
    // "clear" action, since they're meant as a transient "let me visualize this" aid.
    struct AnnotationDragState
    {
        bool Active = false;
        int StartSquare = -1;
    };
    AnnotationDragState AnnotationDrag;
    std::vector<std::pair<int, int>> AnnotationArrows;
    std::optional<BoardState> LastSeenBoard;

    // Updates Drag/PendingPromotionChoices/AnnotationDrag/AnnotationArrows from this frame's
    // mouse input - the sandbox drag/click-to-move state machine plus the right-click-drag
    // planning-arrow annotation tool. Pure input handling, no rendering (see
    // DrawDragHighlights() below) - split out so BoardStatePanel::Draw() itself reads as a
    // sequence of named steps rather than one large function mixing input and rendering.
    // Callers must only call this while interaction is actually enabled (see Draw()'s
    // interactionEnabled - suppressed while a promotion popup is open).
    void UpdateSandboxInteraction(SandboxSession& sandbox, const BoardState& board, std::optional<int> squareUnderMouse, bool windowHovered);

    // Renders the picked-up square highlight and lichess-style destination dot/ring markers for
    // the current Drag state - a no-op while Drag.CurrentMode == Idle.
    void DrawDragHighlights(ImDrawList* drawList, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, float squareSize) const;

    // Promotion-choice popup opened by UpdateSandboxInteraction()'s resolveDrop() when a drop
    // matched more than one legal move (the 4 promotion entries) - positioned over the
    // destination square, 4 piece-texture buttons. A no-op while PendingPromotionChoices is
    // unset.
    void DrawPromotionPopup(SandboxSession& sandbox, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, float squareSize);

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

void BoardStatePanel::Impl::UpdateSandboxInteraction(SandboxSession& sandbox, const BoardState& board, std::optional<int> squareUnderMouse, bool windowHovered)
{
    DragState& drag = Drag;

    const auto isLegalDestination = [&](int square) { return std::any_of(drag.LegalDestinations.begin(), drag.LegalDestinations.end(), [square](const MoveGenerator::LegalMove& m) { return m.To == square; }); };

    const auto resolveDrop = [&](int destSquare)
    {
        std::vector<MoveGenerator::LegalMove> matches;
        for (const MoveGenerator::LegalMove& move : drag.LegalDestinations)
        {
            if (move.To == destSquare)
                matches.push_back(move);
        }

        drag = DragState{};

        if (matches.empty())
            return;  // illegal drop - snap back (rendering just resumes from sandbox.GetBoard())

        if (matches.size() == 1)
        {
            sandbox.PlayMove(matches.front());
            return;
        }

        PendingPromotionChoices = std::move(matches);
        PendingPromotionSquare = destSquare;
        ImGui::OpenPopup("SandboxPromotionPicker");
    };

    // Left-clicking the board clears any drawn planning arrows - chess.com's own convention
    // (left-click dismisses annotations; only right-click-drag creates/removes them). Fires
    // regardless of what else this click does (pick up a piece, play a move, deselect) -
    // matches "clicking to interact with the board" broadly, not just empty squares.
    if (squareUnderMouse && windowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        AnnotationArrows.clear();

    if (drag.CurrentMode != DragState::Mode::Dragging && ImGui::IsMouseClicked(ImGuiMouseButton_Left) && squareUnderMouse && (drag.CurrentMode != DragState::Mode::Idle || windowHovered))
    {
        const int square = *squareUnderMouse;
        const std::optional<Piece>& clickedPiece = board[square];

        if (drag.CurrentMode == DragState::Mode::PickedUp && isLegalDestination(square))
        {
            resolveDrop(square);
        }
        else if (clickedPiece && clickedPiece->Color == sandbox.GetSideToMove())
        {
            std::vector<MoveGenerator::LegalMove> legalMoves = sandbox.GetLegalMovesFrom(square);
            if (!legalMoves.empty())
            {
                drag.CurrentMode = DragState::Mode::PickedUp;
                drag.PickedSquare = square;
                drag.LegalDestinations = std::move(legalMoves);
            }
            else
            {
                drag = DragState{};
            }
        }
        else
        {
            drag = DragState{};
        }
    }

    if (drag.CurrentMode == DragState::Mode::PickedUp && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
        drag.CurrentMode = DragState::Mode::Dragging;

    if (drag.CurrentMode == DragState::Mode::Dragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left))
    {
        if (squareUnderMouse)
            resolveDrop(*squareUnderMouse);
        else
            drag = DragState{};
    }

    // Right-click-drag draws/toggles a planning arrow (see the "annotation arrows" rendering
    // block in Draw()); a plain right-click tap (no drag - released on the same square it
    // started on) instead falls back to its previous job of deselecting an in-progress sandbox
    // pick-up, so that behavior isn't lost.
    AnnotationDragState& annotationDrag = AnnotationDrag;

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && squareUnderMouse && windowHovered)
    {
        annotationDrag.Active = true;
        annotationDrag.StartSquare = *squareUnderMouse;
    }

    if (annotationDrag.Active && ImGui::IsMouseReleased(ImGuiMouseButton_Right))
    {
        annotationDrag.Active = false;

        if (squareUnderMouse && *squareUnderMouse != annotationDrag.StartSquare)
        {
            const std::pair<int, int> arrow{annotationDrag.StartSquare, *squareUnderMouse};
            std::vector<std::pair<int, int>>& arrows = AnnotationArrows;
            const auto it = std::find(arrows.begin(), arrows.end(), arrow);
            if (it != arrows.end())
                arrows.erase(it);  // dragging the same arrow again removes it
            else
                arrows.push_back(arrow);
        }
        else if (drag.CurrentMode != DragState::Mode::Idle)
        {
            drag = DragState{};
        }
    }
}

void BoardStatePanel::Impl::DrawDragHighlights(ImDrawList* drawList, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, float squareSize) const
{
    if (Drag.CurrentMode == DragState::Mode::Idle)
        return;

    const ImVec2 pickedMin = SquareMin(Drag.PickedSquare % 8, Drag.PickedSquare / 8, blackAtBottom, boardOrigin, squareSize);
    drawList->AddRectFilled(pickedMin, ImVec2(pickedMin.x + squareSize, pickedMin.y + squareSize), kPickedSquareColor);

    // Lichess-style destination markers: a small filled dot for a quiet move, a ring hugging
    // the inside edge of the square for a capture - including en passant, whose destination
    // square is itself empty, so board[move.To] alone wouldn't catch it.
    for (const MoveGenerator::LegalMove& move : Drag.LegalDestinations)
    {
        const ImVec2 center = SquareCenter(move.To, blackAtBottom, boardOrigin, squareSize);
        const bool isCapture = move.IsEnPassant || board[move.To].has_value();
        if (isCapture)
            drawList->AddCircle(center, squareSize * 0.46f, kLegalDestinationColor, 0, squareSize * 0.07f);
        else
            drawList->AddCircleFilled(center, squareSize * 0.16f, kLegalDestinationColor);
    }
}

void BoardStatePanel::Impl::DrawPromotionPopup(SandboxSession& sandbox, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, float squareSize)
{
    if (!PendingPromotionChoices)
        return;

    const int destFile = PendingPromotionSquare % 8;
    const int destRank = PendingPromotionSquare / 8;
    const ImVec2 popupPos = SquareMin(destFile, destRank, blackAtBottom, boardOrigin, squareSize);
    ImGui::SetNextWindowPos(popupPos);

    if (ImGui::BeginPopup("SandboxPromotionPicker"))
    {
        // Copied out (not iterated in place) since a clicked choice mutates/resets
        // PendingPromotionChoices below - iterating the optional's own vector while resetting
        // it mid-loop would be undefined behavior. The board hasn't been mutated by PlayMove
        // yet at this point, so choices.front().From (the pawn's square) still reflects who's
        // promoting.
        const std::vector<MoveGenerator::LegalMove> choices = *PendingPromotionChoices;
        const PieceColor promotingColor = (!choices.empty() && board[choices.front().From]) ? board[choices.front().From]->Color : sandbox.GetSideToMove();

        std::optional<MoveGenerator::LegalMove> chosen;
        for (const MoveGenerator::LegalMove& choice : choices)
        {
            ImGui::PushID(static_cast<int>(*choice.Promotion));
            const GLuint texture = PieceTextures[TextureIndex(Piece{*choice.Promotion, promotingColor})];
            const bool clicked = (texture != 0) ? ImGui::ImageButton("##promo", static_cast<ImTextureID>(texture), ImVec2(squareSize, squareSize)) : ImGui::Button("?", ImVec2(squareSize, squareSize));
            if (clicked)
                chosen = choice;
            ImGui::PopID();
            ImGui::SameLine();
        }

        if (chosen)
        {
            sandbox.PlayMove(*chosen);
            PendingPromotionChoices.reset();
            ImGui::CloseCurrentPopup();
        }

        ImGui::EndPopup();
    }
    else
    {
        // Closed without a selection (e.g. clicked away) - drop the pending state so it
        // doesn't linger and re-suppress interaction forever.
        PendingPromotionChoices.reset();
    }
}

BoardStatePanel::BoardStatePanel(EngineInfoPanel& liveEnginePanel, EngineInfoPanel& sandboxEnginePanel, SandboxSession& sandbox)
    : m_LiveEnginePanel(&liveEnginePanel), m_SandboxEnginePanel(&sandboxEnginePanel), m_Sandbox(&sandbox), m_Impl(std::make_unique<Impl>())
{
    LoadSettings();
}

BoardStatePanel::~BoardStatePanel() = default;

void BoardStatePanel::LoadSettings()
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();
    if (!std::filesystem::exists(path))
        return;

    try
    {
        const inih::INIReader ini(path);

        // Get<T>'s default-value parameter is T&& with T explicitly fixed (not deduced) by the
        // <T> below, so it binds only to rvalues - static_cast produces one from each lvalue
        // member (plain `m_ShowLookaheadArrow` etc. wouldn't compile). Same pattern
        // ControlsPanel::LoadSettings() uses for its own bool settings.
        m_ShowLookaheadArrow = ini.Get<bool>("Display", "ShowLookaheadArrow", static_cast<bool>(m_ShowLookaheadArrow));
        m_ShowAlternateMoves = ini.Get<bool>("Display", "ShowAlternateMoves", static_cast<bool>(m_ShowAlternateMoves));
    }
    catch (const std::exception& e)
    {
        LOG_WARN("BoardStatePanel::LoadSettings: failed to read '{}': {} - using defaults", path, e.what());
    }
}

void BoardStatePanel::SaveSettings() const
{
    const std::string path = ExecutablePathUtil::GetSettingsFilePath().string();

    // Read-merge rather than starting from a blank INIReader: INIWriter::write() always writes
    // out exactly (and only) what's in the INIReader object it's given, so starting blank would
    // silently erase every section ControlsPanel::SaveSettings() owns and writes to this same
    // file (Engine/Strength/Autoplay/etc.) - this panel only ever touches its own "Display"
    // section, on top of whatever's already there. ControlsPanel::SaveSettings() does the
    // identical read-merge for the same reason, in the other direction.
    inih::INIReader ini = SettingsIni::LoadOrEmpty(path, "BoardStatePanel::SaveSettings");

    SettingsIni::UpsertEntry(ini, "Display", "ShowLookaheadArrow", m_ShowLookaheadArrow);
    SettingsIni::UpsertEntry(ini, "Display", "ShowAlternateMoves", m_ShowAlternateMoves);

    SettingsIni::SaveMerged(path, ini, "BoardStatePanel::SaveSettings");
}

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

void BoardStatePanel::Draw(const std::optional<std::string>& liveSuggestedMove, const std::optional<std::string>& lookaheadMove, const std::vector<std::string>& alternateMoves, std::optional<float> accuracyPercent)
{
    ImGui::Begin("Analysis Board");

    // Display-only toggles for the two secondary arrow kinds below - live here (rather than in
    // ControlsPanel) since they're purely about what this window draws, not GameSession/engine
    // behavior. Persisted via SaveSettings()/LoadSettings(), same "Display" ini section as
    // before the move.
    ImGuiUtils::CheckboxTextWrapped("##ShowLookaheadArrow", &m_ShowLookaheadArrow, "Show lookahead arrow");
    ImGui::SameLine();
    ImGuiUtils::CheckboxTextWrapped("##ShowAlternateMoves", &m_ShowAlternateMoves, "Show alternate moves");
    ImGui::Separator();

    const bool sandboxActive = m_Sandbox->IsActive();
    EngineInfoPanel& activeEnginePanel = sandboxActive ? *m_SandboxEnginePanel : *m_LiveEnginePanel;

    // Sandbox controls row - only takes up layout space while there's something to control.
    if (sandboxActive)
    {
        ImGui::Text("Exploring %zu move(s)", m_Sandbox->HistoryLength());
        ImGui::SameLine();
        if (ImGui::Button("Undo"))
            m_Sandbox->UndoLastMove();
        ImGui::SameLine();
        if (ImGui::Button("Reset"))
            m_Sandbox->ResetToLive();
        ImGui::Separator();
    }

    const std::optional<EngineInfoPanel::MateInfo> mateInfo = activeEnginePanel.GetMateInfo();

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
    DrawEvalBar(drawList, barMin, barMax, activeEnginePanel.GetWhiteWinFraction(), m_Sandbox->IsBlackAtBottom());

    const ImVec2 boardOrigin(panelOrigin.x + kEvalBarWidth + kEvalBarGap, panelOrigin.y);
    const ImVec2 boardEnd(boardOrigin.x + boardSize, boardOrigin.y + boardSize);

    const bool blackAtBottom = m_Sandbox->IsBlackAtBottom();
    const BoardState& board = m_Sandbox->GetBoard();

    // User-drawn annotation arrows are meant as a transient "let me visualize this" aid, not a
    // permanent record - drop them the moment the position they were drawn on actually changes
    // (a move played on either the live game or the sandbox, an undo/reset, a resync), rather
    // than requiring an explicit clear action.
    if (m_Impl->LastSeenBoard && *m_Impl->LastSeenBoard != board)
        m_Impl->AnnotationArrows.clear();
    m_Impl->LastSeenBoard = board;

    DrawBoardBackground(drawList, m_Impl->BoardTexture, boardOrigin, boardEnd, squareSize, blackAtBottom);

    // Sandbox mouse interaction - a drag/click always targets the sandbox layer, seeding it
    // (via GameSession's live position, already mirrored into m_Sandbox when inactive) on the
    // very first move - never the live site. Suppressed entirely while a promotion popup is
    // open, so a click behind the popup can't also start picking up a new piece. squareUnderMouse
    // is also read further below (the annotation-arrow live preview), so it's computed here
    // rather than inside UpdateSandboxInteraction() itself.
    const bool interactionEnabled = !m_Impl->PendingPromotionChoices.has_value();
    const ImVec2 mousePos = ImGui::GetIO().MousePos;
    const std::optional<int> squareUnderMouse = SquareAtScreenPos(mousePos, boardOrigin, squareSize, blackAtBottom);
    const bool windowHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);

    if (interactionEnabled)
        m_Impl->UpdateSandboxInteraction(*m_Sandbox, board, squareUnderMouse, windowHovered);

    m_Impl->DrawDragHighlights(drawList, board, blackAtBottom, boardOrigin, squareSize);

    const std::optional<std::string> primarySuggestedMove = sandboxActive ? m_Sandbox->GetSuggestedMove() : liveSuggestedMove;
    const std::optional<SuggestedSquares> suggested = ComputeSuggestedSquares(primarySuggestedMove, blackAtBottom, boardOrigin, squareSize);
    if (suggested)
    {
        drawList->AddRectFilled(suggested->FromMin, suggested->FromMax, kSourceHighlightColor);
        drawList->AddRectFilled(suggested->ToMin, suggested->ToMax, kDestHighlightColor);
    }

    const std::optional<int> checkedKingSquare = m_Sandbox->GetCheckedKingSquare();
    if (checkedKingSquare)
    {
        const ImVec2 checkMin = SquareMin(*checkedKingSquare % 8, *checkedKingSquare / 8, blackAtBottom, boardOrigin, squareSize);
        const ImVec2 checkMax(checkMin.x + squareSize, checkMin.y + squareSize);
        drawList->AddRectFilled(checkMin, checkMax, kCheckHighlightColor);
    }

    for (int visualRow = 0; visualRow < 8; ++visualRow)
    {
        for (int visualCol = 0; visualCol < 8; ++visualCol)
        {
            const int file = blackAtBottom ? (7 - visualCol) : visualCol;
            const int rank = blackAtBottom ? visualRow : (7 - visualRow);
            const int square = SquareIndex(file, rank);

            // Skip the piece currently being dragged here - redrawn below, centered on the
            // cursor, so it renders on top of everything else instead of under the highlights.
            if (m_Impl->Drag.CurrentMode == Impl::DragState::Mode::Dragging && square == m_Impl->Drag.PickedSquare)
                continue;

            const std::optional<Piece>& piece = board[square];
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

    if (m_Impl->Drag.CurrentMode == Impl::DragState::Mode::Dragging)
    {
        const std::optional<Piece>& draggedPiece = board[m_Impl->Drag.PickedSquare];
        const GLuint texture = draggedPiece ? m_Impl->PieceTextures[TextureIndex(*draggedPiece)] : 0;
        if (texture != 0)
        {
            const ImVec2 half(squareSize * 0.5f, squareSize * 0.5f);
            drawList->AddImage(static_cast<ImTextureID>(texture), ImVec2(mousePos.x - half.x, mousePos.y - half.y), ImVec2(mousePos.x + half.x, mousePos.y + half.y));
        }
    }

    // Alternate candidate moves (see GameSession::GetAlternateMoves()/SandboxSession::
    // GetAlternateMoves()) - "other possible moves that aren't necessarily the best," each its
    // own color, drawn first/thinnest so the lookahead and primary arrows both still read as
    // more prominent on top of them. Meaningful in both the live and sandbox positions (unlike
    // the lookahead arrow below) - the sandbox's own dedicated engine gets the same MultiPV
    // treatment as the live one (see App). Gated behind m_ShowAlternateMoves so it's a toggle,
    // not something forced on the player.
    if (m_ShowAlternateMoves)
    {
        const std::vector<std::string> activeAlternateMoves = sandboxActive ? m_Sandbox->GetAlternateMoves() : alternateMoves;
        const float alternateThickness = std::max(squareSize * 0.07f, 2.0f);
        for (std::size_t i = 0; i < activeAlternateMoves.size(); ++i)
        {
            const std::optional<SuggestedSquares> alternate = ComputeSuggestedSquares(activeAlternateMoves[i], blackAtBottom, boardOrigin, squareSize);
            if (!alternate)
                continue;

            const ImU32 color = kAlternateArrowColors[std::min(i, std::size(kAlternateArrowColors) - 1)];
            DrawArrow(drawList, alternate->FromCenter, alternate->ToCenter, color, alternateThickness);
        }
    }

    // Lookahead arrow drawn before the primary one so the primary arrow still reads as most
    // prominent - the anticipated reply to the primary suggestion (see GameSession::
    // GetLookaheadMove()/SandboxSession::GetLookaheadMove()), meaningful in both the live and
    // sandbox positions (unlike the sandbox check earlier in this file's history - the sandbox
    // engine now gets the same lookahead treatment as the live one). Gated behind
    // m_ShowLookaheadArrow so it's a toggle, not something forced on the player.
    if (m_ShowLookaheadArrow)
    {
        const std::optional<std::string> activeLookaheadMove = sandboxActive ? m_Sandbox->GetLookaheadMove() : lookaheadMove;
        const std::optional<SuggestedSquares> lookahead = ComputeSuggestedSquares(activeLookaheadMove, blackAtBottom, boardOrigin, squareSize);
        if (lookahead)
            DrawArrow(drawList, lookahead->FromCenter, lookahead->ToCenter, kLookaheadArrowColor, std::max(squareSize * 0.08f, 2.5f));
    }

    // Primary arrow drawn last (among arrows) so it sits on top of the pieces/lookahead arrow
    // instead of being hidden under them. Red instead of the default orange when mateInfo is
    // set - primarySuggestedMove and activeEnginePanel's latest search info both come from the
    // same ongoing search stream, so whenever a forced mate is being reported, this arrow is
    // (or is about to become) the move that leads to it.
    if (suggested)
        DrawArrow(drawList, suggested->FromCenter, suggested->ToCenter, mateInfo ? kMatingArrowColor : kArrowColor, std::max(squareSize * 0.1f, 3.0f));

    // User-drawn planning arrows (see the right-click-drag handling in UpdateSandboxInteraction())
    // - drawn on top of the engine's own arrows since they're the player's own active
    // annotations.
    constexpr ImU32 kAnnotationArrowColor = IM_COL32(0x15, 0x78, 0x1B, 215);
    const float annotationThickness = std::max(squareSize * 0.09f, 2.5f);
    for (const auto& [fromSquare, toSquare] : m_Impl->AnnotationArrows)
        DrawArrow(drawList, SquareCenter(fromSquare, blackAtBottom, boardOrigin, squareSize), SquareCenter(toSquare, blackAtBottom, boardOrigin, squareSize), kAnnotationArrowColor, annotationThickness);

    // Live preview of the arrow currently being drawn (right button still held, dragged past
    // its start square) - not yet committed to AnnotationArrows until release.
    if (m_Impl->AnnotationDrag.Active && squareUnderMouse && *squareUnderMouse != m_Impl->AnnotationDrag.StartSquare)
    {
        DrawArrow(drawList, SquareCenter(m_Impl->AnnotationDrag.StartSquare, blackAtBottom, boardOrigin, squareSize), SquareCenter(*squareUnderMouse, blackAtBottom, boardOrigin, squareSize), kAnnotationArrowColor,
                  annotationThickness);
    }

    // On-board "mate in N" banner, top-left corner of the board itself rather than buried in
    // the score text below - drawn last so it sits above the board/pieces/arrow.
    if (mateInfo)
    {
        char banner[48];
        std::snprintf(banner, sizeof(banner), "%s mates in %d", mateInfo->WhiteIsMating ? "White" : "Black", mateInfo->DistanceInMoves);
        const ImVec2 textSize = ImGui::CalcTextSize(banner);
        const ImVec2 bannerMin(boardOrigin.x + 4.0f, boardOrigin.y + 4.0f);
        const ImVec2 bannerMax(bannerMin.x + textSize.x + 12.0f, bannerMin.y + textSize.y + 8.0f);
        drawList->AddRectFilled(bannerMin, bannerMax, kMateBannerBg, 4.0f);
        drawList->AddText(ImVec2(bannerMin.x + 6.0f, bannerMin.y + 4.0f), kMateBannerText, banner);
    }

    // Reserves layout space for the eval bar + board so the window sizes/scrolls correctly -
    // everything above was drawn directly via the draw list, not through any layout-owning
    // widget.
    ImGui::Dummy(ImVec2(kEvalBarWidth + kEvalBarGap + boardSize, boardSize));

    // Promotion picker popup - positioned over the destination square, 4 piece-texture
    // buttons. Opened by UpdateSandboxInteraction()'s resolveDrop() above when a drop matched
    // more than one legal move.
    m_Impl->DrawPromotionPopup(*m_Sandbox, board, blackAtBottom, boardOrigin, squareSize);

    if (accuracyPercent)
        ImGui::Text("Accuracy: %.1f%%", *accuracyPercent);
    else
        ImGui::TextDisabled("Accuracy: -");

    ImGui::Separator();
    activeEnginePanel.DrawContents();

    ImGui::End();
}
