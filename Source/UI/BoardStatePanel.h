#pragma once

#include "ChessBoardWidget.h"

#include <optional>
#include <string>
#include <vector>

class EngineInfoPanel;
class SandboxSession;
class ChessPieceTextures;

// Renders the tracked live position (window title "Live Analysis Board") as a real chessboard
// - checkerboard squares plus piece images - rather than a screenshot; a sanity-check view so a
// tracking desync is visible at a glance instead of only in log text. Also consolidates the
// engine's read on the position alongside the board it's about: a vertical eval bar, an
// on-board arrow for the engine's suggestion, and EngineInfoPanel::DrawContents()'s
// depth/score/PV/best-move text, all in this one window rather than split across separate ones.
// A second, entirely independent tool for analyzing arbitrary positions lives in
// AnalysisBoardPanel - the two share their board/piece-drag rendering via ChessBoardWidget (see
// its own comment) but otherwise know nothing about each other.
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
    BoardStatePanel(EngineInfoPanel& liveEnginePanel, EngineInfoPanel& sandboxEnginePanel, SandboxSession& sandbox, const ChessPieceTextures& textures);

    // Writes this panel's own display toggles (m_ShowLookaheadArrow/m_ShowAlternateMoves) to
    // settings.ini (see ExecutablePathUtil::GetSettingsFilePath()), overwriting whatever was
    // there - same pattern as ControlsPanel::SaveSettings(), just for the subset of settings
    // this panel itself owns and draws checkboxes for. Called once by App at shutdown.
    void SaveSettings() const;

    // liveSuggestedMove is the live engine's current-turn UCI suggestion (see GameSession::
    // GetSuggestedMove()) - drawn as the primary on-board arrow when no hypothetical line is
    // active. lookaheadMove (see GameSession::GetLookaheadMove()) is drawn as a second,
    // visually distinct arrow one ply beyond it - shown whenever available and the "Show
    // lookahead arrow" checkbox (drawn by this panel itself) is checked, in both the live
    // position and (from SandboxSession::GetLookaheadMove(), the sandbox's own dedicated
    // engine) a hypothetical one. alternateMoves (see GameSession::GetAlternateMoves()) are the
    // live engine's other candidate moves beyond the primary suggestion, each drawn as its own
    // arrow in its own color - shown whenever the "Show alternate moves" checkbox is checked,
    // likewise in both the live and (from the sandbox's own engine) hypothetical positions.
    // accuracyPercent (see GameSession::GetAccuracyPercent()) is always shown from the live
    // game, regardless of sandbox state - nullopt draws a placeholder rather than being omitted.
    void Draw(const std::optional<std::string>& liveSuggestedMove, const std::optional<std::string>& lookaheadMove, const std::vector<std::string>& alternateMoves, std::optional<float> accuracyPercent);

private:
    // Restores m_ShowLookaheadArrow/m_ShowAlternateMoves from settings.ini, if it exists (a
    // fresh install/deleted file just keeps the in-class defaults). Called once from the
    // constructor.
    void LoadSettings();

    EngineInfoPanel* m_LiveEnginePanel = nullptr;
    EngineInfoPanel* m_SandboxEnginePanel = nullptr;
    SandboxSession* m_Sandbox = nullptr;
    const ChessPieceTextures* m_Textures = nullptr;

    // Display-only toggles for the lookahead/alternate-move arrows - neither affects
    // GameSession/engine behavior at all, purely what gets drawn. Default on, matching this
    // app's behavior before these became togglable. Drawn as checkboxes directly in Draw(),
    // right in the window they affect, rather than in ControlsPanel (which owns settings that
    // actually change game/engine behavior).
    bool m_ShowLookaheadArrow = true;
    bool m_ShowAlternateMoves = true;

    // Board/piece rendering plus drag-to-move interaction/promotion popup - see its own
    // comment. One instance owned here (not shared with AnalysisBoardPanel's own), so its
    // interaction state is correctly scoped to this board alone.
    ChessBoardWidget m_Widget;
};
