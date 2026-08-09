#pragma once

#include "Engine/EngineController.h"
#include "Game/GameSession.h"
#include "UI/AppWindow.h"
#include "UI/BoardStatePanel.h"
#include "UI/ControlsPanel.h"
#include "UI/EngineInfoPanel.h"
#include "UI/LogPanel.h"

#include <chrono>

// Owns the whole application: window/ImGui setup, logging sinks, the engine + game session,
// every panel, and the main loop itself - main() just constructs one of these and calls Run().
class App
{
public:
    App();

    // Runs until the window is closed. Returns the process exit code (0 on a normal close,
    // non-zero if window/ImGui setup failed).
    int Run();

private:
    void InitLogging();
    void PollGameSession();

    // Builds a fixed default dock layout (Controls left, Tracked Board large top-right, Log
    // small bottom-right) via ImGui's DockBuilder API, called once on the very first frame -
    // see Run(). Deliberately unconditional (not "only if no saved layout exists"): the
    // in-repo default otherwise leaves whatever ad-hoc arrangement a prior session's
    // imgui.ini happened to save, which is exactly the cramped/wasted-space layout this
    // exists to fix. The user can still freely rearrange for the rest of the session -
    // it's just not persisted across restarts.
    void SetupDefaultDockLayout(unsigned int dockspaceId);

    // Declaration order matters here: members are constructed in this order (and destroyed in
    // reverse), and m_BoardStatePanel/m_GameSession/m_ControlsPanel's constructors take
    // references (to m_EnginePanel, m_Controller, and m_Controller/m_GameSession
    // respectively) that must already be alive.
    LogPanel m_LogPanel;
    AppWindow m_Window;
    EngineInfoPanel m_EnginePanel;
    BoardStatePanel m_BoardStatePanel;  // also draws m_EnginePanel's contents - see its header
    EngineController m_Controller;
    GameSession m_GameSession;
    ControlsPanel m_ControlsPanel;

    std::chrono::steady_clock::time_point m_LastPollTime;
    bool m_LayoutInitialized = false;
};
