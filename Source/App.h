#pragma once

#include "Engine/EngineController.h"
#include "Game/GameSession.h"
#include "Game/SandboxSession.h"
#include "UI/AppWindow.h"
#include "UI/BoardStatePanel.h"
#include "UI/ControlsPanel.h"
#include "UI/EngineInfoPanel.h"
#include "UI/LogPanel.h"

#include <chrono>
#include <cstdint>
#include <optional>

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

    // Builds a fixed default dock layout (Controls left, Analysis Board large top-right, Log
    // small bottom-right) via ImGui's DockBuilder API - called once, on the very first frame,
    // but only when Run() finds no saved imgui.ini from a previous session (see its
    // hasSavedLayout check). A fresh install otherwise starts from ImGui's undocked fallback
    // (every window floating wherever it last defaulted to) instead of this sensible
    // arrangement. Once a saved imgui.ini exists - whether from a previous run or this one,
    // the moment the user rearranges anything - ImGui's own automatic load/autosave (see
    // AppWindow::Init()'s io.IniFilename) takes over for good: this is never called again for
    // that install, so the user's own layout is restored on every later launch instead of
    // being reset back to this default.
    void SetupDefaultDockLayout(unsigned int dockspaceId);

    // Declaration order matters here: members are constructed in this order (and destroyed in
    // reverse), and several constructors take references to earlier members that must already
    // be alive: m_SandboxSession needs m_SandboxController; m_BoardStatePanel needs
    // m_EnginePanel, m_SandboxEnginePanel, and m_SandboxSession; m_GameSession needs
    // m_Controller; m_ControlsPanel needs m_Controller and m_GameSession.
    LogPanel m_LogPanel;
    AppWindow m_Window;
    EngineInfoPanel m_EnginePanel;
    EngineInfoPanel m_SandboxEnginePanel;
    EngineController m_SandboxController;  // dedicated engine process for sandbox "what-if" analysis - never touches the live game's own search
    SandboxSession m_SandboxSession;
    BoardStatePanel m_BoardStatePanel;  // also draws m_EnginePanel/m_SandboxEnginePanel's contents - see its header
    EngineController m_Controller;
    GameSession m_GameSession;
    ControlsPanel m_ControlsPanel;

    std::chrono::steady_clock::time_point m_LastPollTime;
    bool m_LayoutInitialized = false;

    // Compared each frame against GameSession::GetPositionGeneration() to know when to resync
    // m_SandboxSession to the live position - nullopt only before the very first frame, so that
    // frame always syncs (matching pre-connect behavior: BoardStatePanel shows GameSession's
    // never-Reset() empty board until ConnectToSite() runs).
    std::optional<std::uint64_t> m_LastSandboxGeneration;
};
