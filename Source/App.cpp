#include "App.h"

#include "Chess/MoveGenerator.h"
#include "Engine/ExecutablePathUtil.h"
#include "Logging/Log.h"
#include "UI/ImGuiLogSink.h"
#include "Version.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

// DockBuilder (SetupDefaultDockLayout) is part of ImGui's internal API - stable enough for
// this narrow use, but not guaranteed compatible across ImGui versions like the rest of this file.
#include <imgui_internal.h>

#include <cfloat>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace
{
std::string TimestampedLogFilename()
{
    const std::time_t time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif

    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "ChessAssist_%Y%m%d_%H%M%S.log", &tm);
    return buffer;
}
}  // namespace

App::App()
    : m_SandboxSession(m_SandboxController), m_BoardStatePanel(m_EnginePanel, m_SandboxEnginePanel, m_SandboxSession, m_PieceTextures), m_AnalysisSession(m_AnalysisController), m_AnalysisBoardPanel(m_AnalysisEnginePanel, m_AnalysisSession, m_PieceTextures), m_GameSession(m_Controller), m_ControlsPanel(m_Controller, m_GameSession)
{
    InitLogging();
}

void App::InitLogging()
{
    // Attached before anything else runs so every log line (including any early startup
    // failures below) makes it into the in-app Log panel, not just the console.
    spdlog::default_logger()->sinks().push_back(std::make_shared<ImGuiLogSinkMt>(m_LogPanel));

    // One log file per run, next to the executable, so a session can be inspected or shared
    // after the fact without having to have kept the app open and the Log panel visible.
    const std::filesystem::path logsDir = ExecutablePathUtil::GetLogsDirectory();
    std::error_code logsDirError;
    std::filesystem::create_directories(logsDir, logsDirError);
    if (!logsDirError)
    {
        spdlog::default_logger()->sinks().push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>((logsDir / TimestampedLogFilename()).string()));
        spdlog::flush_on(spdlog::level::info);
    }
}

int App::Run()
{
    LOG_INFO("ChessAssist {}", ChessAssist::kVersion);

    if (!m_Window.Init(1920, 1080, std::string("Chess Assist ") + ChessAssist::kVersion))
    {
        LOG_ERROR("Failed to initialize application window");
        return 1;
    }

    // Requires a live GL context (created by m_Window.Init() above), so can't happen any
    // earlier - see ChessPieceTextures::LoadTextures()'s comment. Loaded once and shared by
    // both m_BoardStatePanel and m_AnalysisBoardPanel.
    m_PieceTextures.LoadTextures();

    // Checked once, right after Init() (which points ImGui's io.IniFilename at this path) but
    // before the first frame loads it - if a saved imgui.ini exists, SetupDefaultDockLayout()
    // below must NOT stomp the user's own arrangement.
    const bool hasSavedLayout = std::filesystem::exists(ExecutablePathUtil::GetImGuiIniFilePath());

    // UCI score is relative to the analyzing side, not White - GetRequestedSide() lets the
    // panel flip the sign for the conventional display. Also fans out to GameSession's premove
    // detection, since EngineController only supports one OnInfo subscriber.
    m_Controller.SetOnInfo([this](const SearchInfo& info) {
        m_EnginePanel.UpdateInfo(info, m_GameSession.GetRequestedSide());
        m_GameSession.OnEngineInfo(info);
    });

    // EngineController supports only one OnBestMove subscriber, so this fans the result out to
    // both the panel display and GameSession's autoplay handling (a no-op unless autoplay is
    // enabled and it's the tracked player's turn).
    m_Controller.SetOnBestMove([this](const BestMoveResult& result) {
        m_EnginePanel.UpdateBestMove(result);
        m_GameSession.OnEngineBestMove(result);
    });

    // Passes the (possibly settings.ini-restored) configured path rather than "" so a
    // persisted custom engine path takes effect immediately instead of being silently
    // overridden by the bundled default - see ControlsPanel::GetEnginePath()'s comment.
    m_ControlsPanel.RestartEngine(m_ControlsPanel.GetEnginePath());

    // Second, independent Stockfish process dedicated to the sandbox's "what-if" analysis - no
    // Elo/Blitz/book/custom-path settings, just the bundled default, so exploring a hypothetical
    // line never disrupts or competes with m_Controller's own live-game analysis loop above.
    if (const auto sandboxStartResult = m_SandboxController.Start(); !sandboxStartResult)
        LOG_ERROR("Failed to start sandbox engine: {}", sandboxStartResult.error().Message);
    else
        GameSession::ConfigureMultiPv(m_SandboxController);

    m_SandboxController.SetOnInfo([this](const SearchInfo& info) {
        m_SandboxEnginePanel.UpdateInfo(info, m_SandboxSession.GetRequestedSide());
        m_SandboxSession.OnEngineInfo(info);
    });
    m_SandboxController.SetOnBestMove([this](const BestMoveResult& result) {
        m_SandboxEnginePanel.UpdateBestMove(result);
        m_SandboxSession.OnEngineBestMove(result);
    });

    // Third, independent Stockfish process dedicated to the free-standing analysis board - same
    // isolation reasoning as the sandbox controller above, just for a position that's unrelated
    // to the live game entirely rather than a hypothetical continuation of it.
    if (const auto analysisStartResult = m_AnalysisController.Start(); !analysisStartResult)
        LOG_ERROR("Failed to start analysis engine: {}", analysisStartResult.error().Message);
    else
        GameSession::ConfigureMultiPv(m_AnalysisController);

    // Fires the analysis board's first real search - m_AnalysisSession was constructed before
    // the engine process existed, so it skipped requesting analysis initially (see
    // AnalysisBoardSession's constructor). Reset() re-analyzes the current position now that
    // the engine exists; it's a no-op on the already-empty history.
    m_AnalysisSession.Reset();

    m_AnalysisController.SetOnInfo([this](const SearchInfo& info) {
        m_AnalysisEnginePanel.UpdateInfo(info, m_AnalysisSession.GetRequestedSide());
        m_AnalysisSession.OnEngineInfo(info);
    });
    m_AnalysisController.SetOnBestMove([this](const BestMoveResult& result) {
        m_AnalysisEnginePanel.UpdateBestMove(result);
        m_AnalysisSession.OnEngineBestMove(result);
    });

    constexpr std::chrono::milliseconds kPollInterval{500};
    m_LastPollTime = std::chrono::steady_clock::now();

    while (!m_Window.ShouldClose())
    {
        m_Window.NewFrame();
        DrawMainMenuBar();
        DrawAboutPopup();
        const unsigned int dockspaceId = m_Window.SetupDockspace();

        if (!m_LayoutInitialized)
        {
            if (hasSavedLayout)
                LOG_INFO("Restoring dock layout saved from a previous session");
            else
            {
                LOG_INFO("No saved dock layout found - applying the default one");
                SetupDefaultDockLayout(dockspaceId);
            }
            m_LayoutInitialized = true;
        }

        // Resyncs the sandbox to the live position whenever it's changed (a real move landed,
        // or the game reset) - cheap uint64 compare, so done unconditionally every frame rather
        // than only on the 500ms poll cadence below. See GameSession::GetPositionGeneration().
        const std::uint64_t positionGeneration = m_GameSession.GetPositionGeneration();
        if (!m_LastSandboxGeneration || *m_LastSandboxGeneration != positionGeneration)
        {
            m_SandboxSession.SyncToLivePosition(
                MoveGenerator::PositionState{m_GameSession.GetTrackedBoard(), m_GameSession.GetTracker().GetSideToMove(), m_GameSession.GetCastlingRights(), m_GameSession.GetEnPassantTarget()},
                m_GameSession.IsBlackAtBottom());
            m_LastSandboxGeneration = positionGeneration;
        }

        m_ControlsPanel.Draw();
        m_BoardStatePanel.Draw(m_GameSession.GetSuggestedMove(), m_GameSession.GetLookaheadMove(), m_GameSession.GetAlternateMoves(), m_GameSession.GetAccuracyPercent());
        m_AnalysisBoardPanel.Draw();
        m_LogPanel.Draw();

        const auto now = std::chrono::steady_clock::now();
        if (m_GameSession.IsConnected() && now - m_LastPollTime >= kPollInterval)
        {
            m_LastPollTime = now;
            PollGameSession();
        }

        m_GameSession.Tick();

        m_Window.EndFrame();
    }

    m_ControlsPanel.SaveSettings();
    m_BoardStatePanel.SaveSettings();
    m_AnalysisBoardPanel.SaveSettings();
    m_LogPanel.SaveSettings();

    m_SandboxController.StopSearch();
    m_SandboxController.Shutdown();
    m_AnalysisController.StopSearch();
    m_AnalysisController.Shutdown();
    m_Controller.StopSearch();
    m_Controller.Shutdown();
    m_Window.Shutdown();

    return 0;
}

void App::SetupDefaultDockLayout(unsigned int dockspaceId)
{
    const ImGuiID id = static_cast<ImGuiID>(dockspaceId);

    // ImGuiDockNodeFlags_DockSpace is part of ImGui's private flags enum, a different type
    // from the public ImGuiDockNodeFlags_PassthruCentralNode - the explicit cast avoids a
    // deprecated-enum-enum-conversion warning from ORing the two directly.
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, static_cast<ImGuiDockNodeFlags>(ImGuiDockNodeFlags_DockSpace) | ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::DockBuilderSetNodeSize(id, ImGui::GetMainViewport()->WorkSize);

    // Log spans the full width along the bottom, split off before the left/right split below -
    // Controls' content is nowhere near tall enough to fill a full-height column, so giving Log
    // that reclaimed strip (instead of confining it under just the board) puts the space to use
    // rather than leaving it empty under Controls.
    ImGuiID logId = 0;
    ImGuiID topId = 0;
    ImGui::DockBuilderSplitNode(id, ImGuiDir_Down, 0.22f, &logId, &topId);

    // Controls (small/fixed content) gets a narrow left column; the board (primary content)
    // gets the remaining area.
    ImGuiID leftId = 0;
    ImGuiID rightId = 0;
    ImGui::DockBuilderSplitNode(topId, ImGuiDir_Left, 0.22f, &leftId, &rightId);

    ImGui::DockBuilderDockWindow("Controls", leftId);
    // Both docked to the same node so they appear as tabs in one panel area - the tracked live
    // game and the free-standing analysis tool.
    ImGui::DockBuilderDockWindow("Live Analysis Board", rightId);
    ImGui::DockBuilderDockWindow("Analysis Board", rightId);
    ImGui::DockBuilderDockWindow("Log", logId);

    ImGui::DockBuilderFinish(id);
}

void App::DrawMainMenuBar()
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("Window"))
    {
        bool controlsOpen = m_ControlsPanel.IsOpen();
        if (ImGui::MenuItem("Controls", nullptr, &controlsOpen))
            m_ControlsPanel.SetOpen(controlsOpen);

        bool liveBoardOpen = m_BoardStatePanel.IsOpen();
        if (ImGui::MenuItem("Live Analysis Board", nullptr, &liveBoardOpen))
            m_BoardStatePanel.SetOpen(liveBoardOpen);

        bool analysisBoardOpen = m_AnalysisBoardPanel.IsOpen();
        if (ImGui::MenuItem("Analysis Board", nullptr, &analysisBoardOpen))
            m_AnalysisBoardPanel.SetOpen(analysisBoardOpen);

        bool logOpen = m_LogPanel.IsOpen();
        if (ImGui::MenuItem("Log", nullptr, &logOpen))
            m_LogPanel.SetOpen(logOpen);

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help"))
    {
        if (ImGui::MenuItem("About ChessAssist"))
            m_ShowAboutPopup = true;

        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void App::DrawAboutPopup()
{
    if (m_ShowAboutPopup)
    {
        ImGui::OpenPopup("About ChessAssist");
        m_ShowAboutPopup = false;
    }

    // AlwaysAutoResize only sizes the window to fit its *body* content - it doesn't account for
    // the titlebar text's own width, so without a minimum width constraint here, narrow body
    // content (as below) lets the window shrink enough to clip "About ChessAssist" in the
    // titlebar itself.
    const float minWidth = ImGui::CalcTextSize("About ChessAssist").x + ImGui::GetStyle().WindowPadding.x * 2.0f + 40.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(minWidth, 0.0f), ImVec2(FLT_MAX, FLT_MAX));
    if (!ImGui::BeginPopupModal("About ChessAssist", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    ImGui::Text("ChessAssist");
    ImGui::TextDisabled("Version %s", ChessAssist::kVersion);
    ImGui::Separator();
    ImGui::TextWrapped("A live chess.com/Lichess analysis and autoplay assistant, powered by a bundled Stockfish.");
    ImGui::TextWrapped("Licensed under the GPLv3.");
    ImGui::Spacing();

    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f)))
        ImGui::CloseCurrentPopup();

    ImGui::EndPopup();
}

void App::PollGameSession()
{
    for (const std::string& move : m_GameSession.Poll())
        LOG_INFO("Detected move: {}", move);
}
