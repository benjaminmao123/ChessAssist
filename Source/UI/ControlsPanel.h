#pragma once

#include "Browser/ChessSiteAdapter.h"

#include <array>
#include <string_view>

class EngineController;
class GameSession;

// The "Controls" window: engine restart, browser launch, site connect/disconnect, and the
// autoplay toggle. Owns the small bits of UI-only state (site selection, engine path text
// buffer, autoplay checkbox) that don't belong on EngineController/GameSession themselves -
// those only track state that's actually load-bearing for their own behavior.
class ControlsPanel
{
public:
    ControlsPanel(EngineController& controller, GameSession& gameSession);

    // Shuts down the engine (if running) and restarts it from enginePath, or the bundled
    // default if empty. Used both by the panel's own "Restart Engine" button and once by App
    // at startup to get the engine running before the first frame.
    void RestartEngine(std::string_view enginePath);

    // Not thread-safe: call once per frame from the UI thread only.
    void Draw();

private:
    // The single "Elo" control spans two independent things that both need reapplying
    // whenever the engine restarts (a fresh process starts unlimited): the engine's own
    // internal strength limiting (UCI_LimitStrength/UCI_Elo, sent directly via
    // EngineController::SetOption) and GameSession's derived movetime/depth preset (see
    // GameSession::SetEloTarget). Called both from the Elo widgets and from RestartEngine().
    void ApplyEloTarget();

    EngineController* m_Controller = nullptr;
    GameSession* m_GameSession = nullptr;

    ChessSite m_SelectedSite = ChessSite::ChessDotCom;
    bool m_AutoplayEnabled = false;
    bool m_BlitzMode = false;
    bool m_PremoveEnabled = false;

    bool m_LimitElo = false;
    int m_Elo = 1500;

    // Pre-filled with the bundled default so the field always shows what's actually running;
    // edit it and click Restart Engine to point at a different UCI-compatible executable.
    std::array<char, 512> m_EngineExecutablePathBuffer{};
};
