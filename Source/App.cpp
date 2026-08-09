#include "App.h"

#include "Engine/ExecutablePathUtil.h"
#include "Logging/Log.h"
#include "UI/ImGuiLogSink.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

// DockBuilder (SetupDefaultDockLayout) is part of Dear ImGui's internal API - stable enough
// in practice for this narrow, standard use (build a default layout once at startup), but
// unlike the rest of this file, changes between ImGui versions aren't guaranteed compatible.
#include <imgui_internal.h>

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
    : m_BoardStatePanel(m_EnginePanel)
    , m_GameSession(m_Controller)
    , m_ControlsPanel(m_Controller, m_GameSession)
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
    if (!m_Window.Init(1920, 1080, "ChessAssist"))
    {
        LOG_ERROR("Failed to initialize application window");
        return 1;
    }

    // Requires a live GL context (created by m_Window.Init() above), so can't happen any
    // earlier - see BoardStatePanel::LoadTextures()'s comment.
    m_BoardStatePanel.LoadTextures();

    // UCI "score" is relative to whichever side the search is analyzing for, not to White -
    // GameSession::GetRequestedSide() (safe from any thread, see its comment) lets the panel
    // flip the sign into the conventional "positive = good for White" display. Also fans out
    // to GameSession's premove detection (see GameSession::OnEngineInfo) - EngineController
    // supports only one OnInfo subscriber, same reason OnBestMove fans out below.
    m_Controller.SetOnInfo([this](const SearchInfo& info) {
        m_EnginePanel.UpdateInfo(info, m_GameSession.GetRequestedSide());
        m_GameSession.OnEngineInfo(info);
    });

    // EngineController supports only one OnBestMove subscriber, so this callback fans the
    // result out to both: the panel display, and GameSession's autoplay handling (a no-op
    // there unless autoplay is enabled and the result is for the tracked player's own turn -
    // see GameSession::OnEngineBestMove).
    m_Controller.SetOnBestMove([this](const BestMoveResult& result) {
        m_EnginePanel.UpdateBestMove(result);
        m_GameSession.OnEngineBestMove(result);
    });

    m_ControlsPanel.RestartEngine("");

    constexpr std::chrono::milliseconds kPollInterval{500};
    m_LastPollTime = std::chrono::steady_clock::now();

    while (!m_Window.ShouldClose())
    {
        const unsigned int dockspaceId = m_Window.BeginFrame();

        if (!m_LayoutInitialized)
        {
            SetupDefaultDockLayout(dockspaceId);
            m_LayoutInitialized = true;
        }

        m_ControlsPanel.Draw();
        m_BoardStatePanel.Draw(m_GameSession.GetTrackedBoard(), m_GameSession.IsBlackAtBottom(), m_GameSession.GetSuggestedMove(), m_GameSession.GetCheckedKingSquare(), m_GameSession.GetAccuracyPercent());
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
    ImGui::DockBuilderSetNodeSize(id, ImGui::GetMainViewport()->Size);

    // Controls (settings, buttons - small/fixed content) gets a narrow left column; Tracked
    // Board (the primary content - board, eval bar, engine info) gets the large remaining
    // area; Log (diagnostic, rarely needs to be as tall as the board) gets a strip under it.
    ImGuiID leftId = 0;
    ImGuiID rightId = 0;
    ImGui::DockBuilderSplitNode(id, ImGuiDir_Left, 0.22f, &leftId, &rightId);

    ImGuiID boardId = 0;
    ImGuiID logId = 0;
    ImGui::DockBuilderSplitNode(rightId, ImGuiDir_Up, 0.75f, &boardId, &logId);

    ImGui::DockBuilderDockWindow("Controls", leftId);
    ImGui::DockBuilderDockWindow("Tracked Board", boardId);
    ImGui::DockBuilderDockWindow("Log", logId);

    ImGui::DockBuilderFinish(id);
}

void App::PollGameSession()
{
    for (const std::string& move : m_GameSession.Poll())
        LOG_INFO("Detected move: {}", move);
}
