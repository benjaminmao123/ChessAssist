#pragma once

#include "Chess/ChessTypes.h"

#include <memory>
#include <optional>
#include <string>

class EngineInfoPanel;

// Renders GameSession's tracked position (from ChessRules::GetBoard()) as a real chessboard -
// checkerboard squares plus piece images - rather than a screenshot; just a sanity-check view
// so a tracking desync is visible at a glance instead of only in log text. Also consolidates
// the engine's read on the position alongside the board it's about: a vertical eval bar, an
// on-board arrow for its suggested move on the tracked player's own turn, and
// EngineInfoPanel::DrawContents()'s depth/score/PV/best-move text, all in this one window
// rather than split across separate ones. Stateless per frame otherwise: called directly with
// the current board each frame from the UI thread, same as the rest of the poll loop - no
// buffering of its own needed since, unlike EngineInfoPanel, nothing here is fed from an async
// callback on another thread.
class BoardStatePanel
{
public:
    explicit BoardStatePanel(EngineInfoPanel& enginePanel);
    ~BoardStatePanel();
    BoardStatePanel(const BoardStatePanel&) = delete;
    BoardStatePanel& operator=(const BoardStatePanel&) = delete;

    // Loads the 12 piece PNGs (wP/wN/wB/wR/wQ/wK/bP/bN/bB/bR/bQ/bK.png) plus the board
    // background (empty_board.png) from Assets/Chess/ into GL textures. Requires a current GL
    // context, so must be called after AppWindow::Init() succeeds - not from the constructor,
    // which runs before the window/context exist. Anything missing or that fails to load is
    // logged once here and Draw() falls back to a plain drawn checkerboard/empty square for
    // it, rather than failing the whole panel.
    void LoadTextures();

    // blackAtBottom draws the board in the same orientation as the live game (see
    // GameSession::IsBlackAtBottom) instead of always assuming White-at-bottom. suggestedMove,
    // if present, is the engine's UCI suggestion for the tracked player's own turn right now
    // (see GameSession::GetSuggestedMove()) - drawn as an arrow from the piece to move to
    // where it should go, plus a highlight on both squares. accuracyPercent (see
    // GameSession::GetAccuracyPercent()) is shown as text alongside the engine info below the
    // board, nullopt drawing a placeholder rather than being omitted.
    void Draw(const BoardState& board, bool blackAtBottom, const std::optional<std::string>& suggestedMove, std::optional<float> accuracyPercent);

private:
    EngineInfoPanel* m_EnginePanel = nullptr;

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
