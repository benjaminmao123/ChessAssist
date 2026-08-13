#pragma once

#include "ChessBoardWidget.h"

#include <optional>
#include <string>
#include <vector>

class EngineInfoPanel;
class SandboxSession;
class ChessPieceTextures;

// Renders the tracked live position (window title "Live Analysis Board") as a real chessboard
// rather than a screenshot - a sanity-check view so a tracking desync is visible at a glance.
// Also consolidates the engine's read on the position: eval bar, suggestion arrow, and
// EngineInfoPanel::DrawContents()'s depth/score/PV/best-move text, all in one window. A second,
// independent tool for analyzing arbitrary positions lives in AnalysisBoardPanel - the two share
// board/piece-drag rendering via ChessBoardWidget but otherwise know nothing about each other.
//
// Also owns the sandbox's mouse interaction: dragging pieces to explore a hypothetical
// continuation locally (see SandboxSession), never touching the live site. Since SandboxSession
// always mirrors the live position while no hypothetical move has been played, this class reads
// board/orientation/check-square straight off it rather than as Draw() parameters. While a
// hypothetical line is active, the eval bar/arrow/PV text switch to the sandbox's own dedicated
// engine (sandboxEnginePanel) so exploring never shows stale live-position analysis.
class BoardStatePanel
{
public:
    BoardStatePanel(EngineInfoPanel& liveEnginePanel, EngineInfoPanel& sandboxEnginePanel, SandboxSession& sandbox, const ChessPieceTextures& textures);

    // Writes this panel's own display toggles (m_ShowLookaheadArrow/m_ShowAlternateMoves) to
    // settings.ini, overwriting whatever was there - same pattern as
    // ControlsPanel::SaveSettings(). Called once by App at shutdown.
    void SaveSettings() const;

    // liveSuggestedMove (see GameSession::GetSuggestedMove()) is the primary on-board arrow when
    // no hypothetical line is active. lookaheadMove is a second, visually distinct arrow one ply
    // beyond it, gated behind the "Show lookahead arrow" checkbox. alternateMoves are the live
    // engine's other candidates, each its own arrow color, gated behind "Show alternate moves".
    // All three fall back to the sandbox's own dedicated engine while a hypothetical line is
    // active. accuracyPercent is always from the live game regardless of sandbox state - nullopt
    // draws a placeholder rather than being omitted.
    void Draw(const std::optional<std::string>& liveSuggestedMove, const std::optional<std::string>& lookaheadMove, const std::vector<std::string>& alternateMoves, std::optional<float> accuracyPercent);

    // Shows/hides the panel - bound to the window's own titlebar close button (via Draw()'s
    // ImGui::Begin(..., &m_Open)) and to App's menu-bar checkbox for reopening it.
    [[nodiscard]] bool IsOpen() const { return m_Open; }
    void SetOpen(bool open) { m_Open = open; }

private:
    // Restores m_ShowLookaheadArrow/m_ShowAlternateMoves from settings.ini, if it exists (a
    // fresh install/deleted file keeps the in-class defaults). Called once from the constructor.
    void LoadSettings();

    EngineInfoPanel* m_LiveEnginePanel = nullptr;
    EngineInfoPanel* m_SandboxEnginePanel = nullptr;
    SandboxSession* m_Sandbox = nullptr;
    const ChessPieceTextures* m_Textures = nullptr;

    // See IsOpen()/SetOpen().
    bool m_Open = true;

    // Display-only toggles for the lookahead/alternate-move arrows - neither affects
    // GameSession/engine behavior, purely what gets drawn. Drawn as checkboxes directly in
    // Draw() rather than in ControlsPanel, which owns settings that change game/engine behavior.
    bool m_ShowLookaheadArrow = true;
    bool m_ShowAlternateMoves = true;

    // Board/piece rendering plus drag-to-move interaction/promotion popup (see ChessBoardWidget).
    // One instance owned here, not shared with AnalysisBoardPanel's own, so interaction state is
    // correctly scoped to this board alone.
    ChessBoardWidget m_Widget;
};
