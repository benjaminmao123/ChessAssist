#pragma once

#include "ChessPieceTextures.h"

#include "Chess/MoveGenerator.h"
#include "Game/IPlayableBoard.h"

#include <imgui.h>

#include <optional>
#include <utility>
#include <vector>

// Layout/color constants shared by every board this app draws (the live board, the
// free-standing analysis board) - declared here (not private to one panel's .cpp) so both read
// consistently. Plain constexpr at header scope gets each including TU its own internal-linkage
// copy - the standard, ODR-safe way to share compile-time constants via a header.
constexpr float kChessBoardEvalBarWidth = 28.0f;
constexpr float kChessBoardEvalBarGap = 12.0f;
constexpr float kChessBoardMinSquareSize = 20.0f;
constexpr ImU32 kChessBoardLightSquareColor = IM_COL32(0xEE, 0xEE, 0xD2, 255);
constexpr ImU32 kChessBoardDarkSquareColor = IM_COL32(0x76, 0x96, 0x56, 255);
constexpr ImU32 kChessBoardSourceHighlightColor = IM_COL32(0xFF, 0xCD, 0x00, 110);
constexpr ImU32 kChessBoardDestHighlightColor = IM_COL32(0x00, 0xC8, 0x5A, 110);
constexpr ImU32 kChessBoardArrowColor = IM_COL32(0xFF, 0x8C, 0x00, 220);
constexpr ImU32 kChessBoardLookaheadArrowColor = IM_COL32(0x3A, 0x8C, 0xFF, 200);
constexpr ImU32 kChessBoardMatingArrowColor = IM_COL32(0xFF, 0x2A, 0x2A, 235);
constexpr ImU32 kChessBoardCheckHighlightColor = IM_COL32(0xFF, 0x1A, 0x1A, 150);
constexpr ImU32 kChessBoardMateBannerBg = IM_COL32(0x20, 0x00, 0x00, 210);
constexpr ImU32 kChessBoardMateBannerText = IM_COL32(0xFF, 0x70, 0x70, 255);
constexpr ImU32 kChessBoardPickedSquareColor = IM_COL32(0xFF, 0xFF, 0x00, 90);
constexpr ImU32 kChessBoardLegalDestinationColor = IM_COL32(0x00, 0x00, 0x00, 140);
constexpr ImU32 kChessBoardAnnotationArrowColor = IM_COL32(0x15, 0x78, 0x1B, 215);

// One color per alternate candidate move (see GameSession::GetAlternateMoves()), in order -
// deliberately distinct from the primary (orange/red) and lookahead (blue) arrow colors, and
// deliberately much more transparent than the primary arrow's alpha 220 - low enough that the
// primary (best) move still reads as the obvious, solid one at a glance, with each further
// alternate fading a bit more to reinforce "less preferred than the last." Alternates beyond
// the array's length reuse its last entry rather than indexing out of bounds.
constexpr ImU32 kChessBoardAlternateArrowColors[] = {
    IM_COL32(0xC9, 0xA8, 0x00, 130),  // 2nd choice - gold
    IM_COL32(0x9C, 0x27, 0xB0, 100),  // 3rd choice - purple
    IM_COL32(0x00, 0xAC, 0xC1, 75),   // 4th choice - teal
};

// Top-left corner of (file, rank)'s square in screen space - canonical board indexing (see
// ChessTypes.h) is always file 0..7 = a..h, rank 0..7 = rank1..8, independent of how it's
// drawn; blackAtBottom flips which screen position that maps to.
[[nodiscard]] ImVec2 ChessBoardSquareMin(int file, int rank, bool blackAtBottom, ImVec2 boardOrigin, float squareSize);

// Inverse of ChessBoardSquareMin(): the canonically-indexed square under a screen-space point,
// or nullopt if the point falls outside the 8x8 board area.
[[nodiscard]] std::optional<int> ChessBoardSquareAtScreenPos(ImVec2 pos, ImVec2 boardOrigin, float squareSize, bool blackAtBottom);

// Center point of a canonically-indexed square in screen space - used for arrows that have no
// UCI move string to derive endpoints from (user-drawn annotations).
[[nodiscard]] ImVec2 ChessBoardSquareCenter(int square, bool blackAtBottom, ImVec2 boardOrigin, float squareSize);

struct ChessBoardSuggestedSquares
{
    ImVec2 FromMin, FromMax, FromCenter;
    ImVec2 ToMin, ToMax, ToCenter;
};

// Resolves a UCI move string (e.g. "e2e4") into screen-space rects/centers for its from/to
// squares - nullopt if suggestedMove is unset or too short to be a UCI move.
[[nodiscard]] std::optional<ChessBoardSuggestedSquares> ChessBoardComputeSuggestedSquares(const std::optional<std::string>& suggestedMove, bool blackAtBottom, ImVec2 boardOrigin, float squareSize);

// Line plus a filled triangular head at `to`, pointing from `from` - used to show a suggested
// move as "drag the piece from here to here".
void ChessBoardDrawArrow(ImDrawList* drawList, ImVec2 from, ImVec2 to, ImU32 color, float thickness);

// Vertical two-tone bar. Whichever color renders at the bottom of the board (see
// blackAtBottom) also fills from the bottom of the bar here, matching the usual lichess/
// chess.com convention of the bar mirroring the board's own orientation. whiteFraction
// nullopt (no search info yet) draws an even split.
void ChessBoardDrawEvalBar(ImDrawList* drawList, ImVec2 barMin, ImVec2 barMax, std::optional<float> whiteFraction, bool blackAtBottom);

// Everything needed to render one interactive chessboard - piece/board texture drawing,
// coordinate labels, drag/click-to-move interaction, right-click-drag planning-arrow
// annotations, and the promotion-choice popup. One instance is owned by *each* panel that draws
// a board (BoardStatePanel, AnalysisBoardPanel) - interaction state is correctly per-board (you
// shouldn't be able to pick up a piece on one board and drop it on the other); only the GL
// textures themselves are actually shared, via the separate ChessPieceTextures each panel also
// takes a reference to. Drives whichever IPlayableBoard the owning panel gives it (SandboxSession
// for the live board, AnalysisBoardSession for the free-standing one) without depending on
// either concretely.
class ChessBoardWidget
{
public:
    // Updates drag/click-to-move state and the right-click-drag annotation-arrow tool from this
    // frame's mouse input. Pure input handling, no rendering (see the Draw* methods below).
    // Callers must only call this while interaction is actually enabled - suppressed while a
    // promotion popup is open, so a click behind the popup can't also start picking up a new
    // piece (see BoardStatePanel::Draw()'s interactionEnabled for the existing pattern).
    void UpdateInteraction(IPlayableBoard& board, std::optional<int> squareUnderMouse, bool windowHovered);

    // Board background (texture, or a plain drawn checkerboard fallback if it failed to load)
    // plus file/rank coordinate labels, the piece-drawing loop (skipping whichever square is
    // currently being dragged), and the dragged piece redrawn centered on mousePos - always
    // needed together, so consolidated into one call.
    void DrawBoardAndPieces(ImDrawList* drawList, const ChessPieceTextures& textures, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, ImVec2 boardEnd, float squareSize, ImVec2 mousePos) const;

    // The picked-up square highlight and lichess-style destination dot/ring markers for the
    // current drag state - a no-op while nothing is picked up.
    void DrawDragHighlights(ImDrawList* drawList, const BoardState& board, bool blackAtBottom, ImVec2 boardOrigin, float squareSize) const;

    // Committed annotation arrows, plus a live preview of the one currently being drawn (right
    // mouse button still held, dragged past its start square).
    void DrawAnnotationArrows(ImDrawList* drawList, bool blackAtBottom, ImVec2 boardOrigin, float squareSize, std::optional<int> squareUnderMouse) const;

    // Promotion-choice popup opened by UpdateInteraction() when a drop matched more than one
    // legal move (the 4 promotion entries) - positioned over the destination square, 4
    // piece-texture buttons. A no-op while nothing is pending.
    void DrawPromotionPopup(IPlayableBoard& board, const ChessPieceTextures& textures, bool blackAtBottom, ImVec2 boardOrigin, float squareSize);

    // Drops any annotation arrows and clears the drag/promotion state the moment the drawn
    // board itself changes to something other than lastSeenBoard (a move played, an undo/
    // reset/resync) - annotations are meant as a transient "let me visualize this" aid, not a
    // permanent record. Callers should pass the board they're about to draw each frame; this
    // updates its own internally-tracked "last seen" copy.
    void ClearAnnotationsIfBoardChanged(const BoardState& board);

private:
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
    DragState m_Drag;

    std::optional<std::vector<MoveGenerator::LegalMove>> m_PendingPromotionChoices;
    int m_PendingPromotionSquare = -1;

    struct AnnotationDragState
    {
        bool Active = false;
        int StartSquare = -1;
    };
    AnnotationDragState m_AnnotationDrag;
    std::vector<std::pair<int, int>> m_AnnotationArrows;
    std::optional<BoardState> m_LastSeenBoard;
};
