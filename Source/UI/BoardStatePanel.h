#pragma once

#include <memory>
#include <optional>
#include <string>

class EngineInfoPanel;
class SandboxSession;

// Renders the tracked position as a real chessboard - checkerboard squares plus piece images -
// rather than a screenshot; a sanity-check view so a tracking desync is visible at a glance
// instead of only in log text. Also consolidates the engine's read on the position alongside
// the board it's about: a vertical eval bar, an on-board arrow for the engine's suggestion, and
// EngineInfoPanel::DrawContents()'s depth/score/PV/best-move text, all in this one window
// rather than split across separate ones.
//
// Also owns the sandbox's mouse interaction: dragging pieces to explore a hypothetical
// continuation locally (see SandboxSession) - never touches the live site. Since SandboxSession
// always mirrors the live tracked position while no hypothetical move has been played, this
// class reads board/orientation/check-square straight off it rather than taking them as Draw()
// parameters - correct whether or not a hypothetical line is active. While a hypothetical line
// IS active, the eval bar/arrow/PV text switch to the sandbox's own dedicated engine
// (sandboxEnginePanel) instead of the live one, so exploring never shows stale/live-position
// analysis under a hypothetical board.
class BoardStatePanel
{
public:
    BoardStatePanel(EngineInfoPanel& liveEnginePanel, EngineInfoPanel& sandboxEnginePanel, SandboxSession& sandbox);
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

    // liveSuggestedMove is the live engine's UCI suggestion for the tracked player's own turn
    // right now (see GameSession::GetSuggestedMove()) - drawn as the primary on-board arrow
    // when no hypothetical line is active. lookaheadMove (see GameSession::GetLookaheadMove())
    // is drawn as a second, visually distinct arrow - the tracked player's planned response to
    // the engine's predicted opponent move - shown only when it's the opponent's turn and no
    // hypothetical line is active. accuracyPercent (see GameSession::GetAccuracyPercent()) is
    // always shown from the live game, regardless of sandbox state - nullopt draws a
    // placeholder rather than being omitted.
    void Draw(const std::optional<std::string>& liveSuggestedMove, const std::optional<std::string>& lookaheadMove, std::optional<float> accuracyPercent);

private:
    EngineInfoPanel* m_LiveEnginePanel = nullptr;
    EngineInfoPanel* m_SandboxEnginePanel = nullptr;
    SandboxSession* m_Sandbox = nullptr;

    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
