#pragma once

#include "../Chess/ChessTypes.h"

// Plain ImGui text/grid rendering of GameSession's tracked position (from
// ChessRules::GetBoard()) - not a screenshot, just a sanity-check view so a tracking
// desync is visible at a glance instead of only in log text. Stateless: called directly
// with the current board each frame from the UI thread, same as the rest of main.cpp's
// poll loop - no buffering needed since, unlike EngineInfoPanel, nothing here is fed from
// an async callback on another thread.
class BoardStatePanel
{
public:
    void Draw(const BoardState& board);
};
