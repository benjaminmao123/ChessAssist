#pragma once

#include "ChessBoardWidget.h"

#include <array>

class EngineInfoPanel;
class AnalysisBoardSession;
class ChessPieceTextures;

// A free-standing position-analysis tool (window title "Analysis Board") - entirely independent
// of the live tracked game: play out (or paste, via FEN) any position from scratch, step
// backward/forward, reset, flip orientation, and get live engine analysis. Backed by
// AnalysisBoardSession; shares board/piece-drag rendering with BoardStatePanel via
// ChessBoardWidget, but owns its own instance since interaction state is correctly per-board.
class AnalysisBoardPanel
{
public:
    AnalysisBoardPanel(EngineInfoPanel& enginePanel, AnalysisBoardSession& session, const ChessPieceTextures& textures);

    // Writes this panel's own display toggles to settings.ini - same pattern as
    // BoardStatePanel::SaveSettings(), just under this panel's own "AnalysisBoard" section so
    // the two panels' identically-named toggles don't collide. Called once by App at shutdown.
    void SaveSettings() const;

    // Not thread-safe: call once per frame from the UI thread only.
    void Draw();

    // Shows/hides the panel - bound to the window's own titlebar close button (via Draw()'s
    // ImGui::Begin(..., &m_Open)) and to App's menu-bar checkbox for reopening it.
    [[nodiscard]] bool IsOpen() const { return m_Open; }
    void SetOpen(bool open) { m_Open = open; }

private:
    // Restores m_ShowLookaheadArrow/m_ShowAlternateMoves from settings.ini, if it exists.
    // Called once from the constructor.
    void LoadSettings();

    EngineInfoPanel* m_EnginePanel = nullptr;
    AnalysisBoardSession* m_Session = nullptr;
    const ChessPieceTextures* m_Textures = nullptr;

    // See IsOpen()/SetOpen().
    bool m_Open = true;

    ChessBoardWidget m_Widget;

    // The FEN paste field's text buffer, plus whether the last attempt to load it failed (drawn
    // as an inline error message until the next successful load, or until the field's contents
    // change - see Draw()).
    std::array<char, 256> m_FenBuffer{};
    bool m_FenLoadFailed = false;

    // Display-only toggles for the lookahead/alternate-move arrows - same meaning as
    // BoardStatePanel's own, just scoped to this independent board/session.
    bool m_ShowLookaheadArrow = true;
    bool m_ShowAlternateMoves = true;
};
