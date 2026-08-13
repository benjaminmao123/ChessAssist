#pragma once

#include "Engine/EngineController.h"
#include "Game/AnalysisBoardSession.h"
#include "Game/GameSession.h"
#include "Game/SandboxSession.h"
#include "UI/AnalysisBoardPanel.h"
#include "UI/AppWindow.h"
#include "UI/BoardStatePanel.h"
#include "UI/ChessPieceTextures.h"
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

    // Builds the fixed default dock layout (Controls left, board large top-right, Log small
    // bottom-right) via ImGui's DockBuilder API. Called once, on the first frame, only when
    // Run() finds no saved imgui.ini (see its hasSavedLayout check) - once one exists, ImGui's
    // own load/autosave takes over for good and this is never called again for that install.
    void SetupDefaultDockLayout(unsigned int dockspaceId);

    // Construction order matters: members are built in this order (destroyed in reverse), and
    // several constructors take references to earlier members - m_SandboxSession needs
    // m_SandboxController; m_BoardStatePanel needs m_EnginePanel, m_SandboxEnginePanel,
    // m_SandboxSession, m_PieceTextures; m_AnalysisSession needs m_AnalysisController;
    // m_AnalysisBoardPanel needs m_AnalysisEnginePanel, m_AnalysisSession, m_PieceTextures;
    // m_GameSession needs m_Controller; m_ControlsPanel needs m_Controller and m_GameSession.
    LogPanel m_LogPanel;
    AppWindow m_Window;

    // Loaded once (see Run()) and shared by both boards below - loading the same 13 PNGs twice
    // would double GPU memory/disk I/O for no benefit.
    ChessPieceTextures m_PieceTextures;

    EngineInfoPanel m_EnginePanel;
    EngineInfoPanel m_SandboxEnginePanel;
    EngineController m_SandboxController;  // dedicated engine process for sandbox "what-if" analysis - never touches the live game's own search
    SandboxSession m_SandboxSession;
    BoardStatePanel m_BoardStatePanel;  // also draws m_EnginePanel/m_SandboxEnginePanel's contents - see its header

    EngineInfoPanel m_AnalysisEnginePanel;
    EngineController m_AnalysisController;  // dedicated engine process for the free-standing analysis board - unrelated to the live game or the sandbox
    AnalysisBoardSession m_AnalysisSession;
    AnalysisBoardPanel m_AnalysisBoardPanel;  // also draws m_AnalysisEnginePanel's contents - see its header

    EngineController m_Controller;
    GameSession m_GameSession;
    ControlsPanel m_ControlsPanel;

    std::chrono::steady_clock::time_point m_LastPollTime;
    bool m_LayoutInitialized = false;

    // Compared each frame against GameSession::GetPositionGeneration() to know when to resync
    // m_SandboxSession to the live position - nullopt only before the first frame, so that
    // frame always syncs.
    std::optional<std::uint64_t> m_LastSandboxGeneration;
};
