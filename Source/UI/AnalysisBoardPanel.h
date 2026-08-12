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

    // Not thread-safe: call once per frame from the UI thread only.
    void Draw();

private:
    EngineInfoPanel* m_EnginePanel = nullptr;
    AnalysisBoardSession* m_Session = nullptr;
    const ChessPieceTextures* m_Textures = nullptr;

    ChessBoardWidget m_Widget;

    // The FEN paste field's text buffer, plus whether the last attempt to load it failed (drawn
    // as an inline error message until the next successful load, or until the field's contents
    // change - see Draw()).
    std::array<char, 256> m_FenBuffer{};
    bool m_FenLoadFailed = false;
};
