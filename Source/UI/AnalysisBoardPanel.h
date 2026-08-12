#pragma once

#include "ChessBoardWidget.h"

#include <array>

class EngineInfoPanel;
class AnalysisBoardSession;
class ChessPieceTextures;

// A free-standing position-analysis tool (window title "Analysis Board", docked as a tab
// alongside BoardStatePanel's "Live Analysis Board") - entirely independent of the live tracked
// game: play out (or paste, via FEN) any position from scratch, step backward/forward through
// what's been played, reset, flip orientation, and get live engine analysis (eval bar,
// suggested-move arrow, depth/score/PV). Backed by AnalysisBoardSession; shares its board/piece-
// drag rendering with BoardStatePanel via ChessBoardWidget (see its own comment) but owns its
// own instance of it, since drag/annotation interaction state is correctly per-board.
class AnalysisBoardPanel
{
public:
    AnalysisBoardPanel(EngineInfoPanel& enginePanel, AnalysisBoardSession& session, const ChessPieceTextures& textures);

    // Writes this panel's own display toggles (m_ShowLookaheadArrow/m_ShowAlternateMoves) to
    // settings.ini (see ExecutablePathUtil::GetSettingsFilePath()), overwriting whatever was
    // there - same pattern as BoardStatePanel::SaveSettings(), just under this panel's own
    // "AnalysisBoard" section so the two panels' identically-named toggles don't collide. Called
    // once by App at shutdown.
    void SaveSettings() const;

    // Not thread-safe: call once per frame from the UI thread only.
    void Draw();

private:
    // Restores m_ShowLookaheadArrow/m_ShowAlternateMoves from settings.ini, if it exists (a
    // fresh install/deleted file just keeps the in-class defaults). Called once from the
    // constructor.
    void LoadSettings();

    EngineInfoPanel* m_EnginePanel = nullptr;
    AnalysisBoardSession* m_Session = nullptr;
    const ChessPieceTextures* m_Textures = nullptr;

    ChessBoardWidget m_Widget;

    // The FEN paste field's text buffer, plus whether the last attempt to load it failed (drawn
    // as an inline error message until the next successful load, or until the field's contents
    // change - see Draw()).
    std::array<char, 256> m_FenBuffer{};
    bool m_FenLoadFailed = false;

    // Display-only toggles for the lookahead/alternate-move arrows - same meaning as
    // BoardStatePanel's own (see its comment), just scoped to this independent board/session.
    // Default on, matching this panel's primary-arrow-only behavior before these existed.
    bool m_ShowLookaheadArrow = true;
    bool m_ShowAlternateMoves = true;
};
