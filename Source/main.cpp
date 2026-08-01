#include "Browser/ChessSiteAdapter.h"
#include "Engine/EngineController.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/GameSession.h"
#include "Logging/Log.h"
#include "UI/AppWindow.h"
#include "UI/BoardStatePanel.h"
#include "UI/EngineInfoPanel.h"
#include "UI/ImGuiLogSink.h"
#include "UI/LogPanel.h"

#include <ixwebsocket/IXNetSystem.h>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <imgui.h>

#include <array>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <memory>
#include <string>
#include <system_error>

namespace
{
constexpr const char* kSiteNames[] = {"Chess.com", "Lichess"};

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

void StartEngine(EngineController& controller, std::string_view customPath)
{
    controller.Shutdown();

    const std::optional<std::filesystem::path> enginePath = customPath.empty() ? std::nullopt : std::optional<std::filesystem::path>(std::filesystem::path(customPath));

    if (const auto startResult = controller.Start(enginePath); !startResult)
        LOG_ERROR("Failed to start engine: {}", startResult.error().Message);
    else
        LOG_INFO("Engine started from {}", enginePath ? enginePath->string() : "bundled default (" + ExecutablePathUtil::GetDefaultStockfishPath().string() + ")");
}
}  // namespace

int main()
{
    ix::initNetSystem();

    // Attached before anything else runs so every log line (including any early startup
    // failures below) makes it into the in-app Log panel, not just the console.
    LogPanel logPanel;
    spdlog::default_logger()->sinks().push_back(std::make_shared<ImGuiLogSinkMt>(logPanel));

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

    AppWindow window;
    if (!window.Init(1920, 1080, "ChessAssist"))
    {
        LOG_ERROR("Failed to initialize application window");
        return 1;
    }

    EngineInfoPanel enginePanel;
    BoardStatePanel boardStatePanel;

    EngineController controller;
    controller.SetOnInfo([&enginePanel](const SearchInfo& info) { enginePanel.UpdateInfo(info); });
    controller.SetOnBestMove([&enginePanel](const BestMoveResult& result) { enginePanel.UpdateBestMove(result); });
    StartEngine(controller, "");

    GameSession gameSession(controller);

    ChessSite selectedSite = ChessSite::ChessDotCom;
    bool autoplayEnabled = false;  // UI placeholder only - see the disabled checkbox below

    // Pre-filled with the bundled default so the field always shows what's actually running;
    // edit it and click Restart Engine to point at a different UCI-compatible executable.
    std::array<char, 512> engineExecutablePathBuffer{};
    const std::string defaultEnginePath = ExecutablePathUtil::GetDefaultStockfishPath().string();
    std::snprintf(engineExecutablePathBuffer.data(), engineExecutablePathBuffer.size(), "%s", defaultEnginePath.c_str());

    constexpr std::chrono::milliseconds kPollInterval{500};
    auto lastPollTime = std::chrono::steady_clock::now();

    while (!window.ShouldClose())
    {
        window.BeginFrame();

        ImGui::Begin("Controls");

        ImGui::Text("Engine: %s", controller.IsRunning() ? "running" : "not running");

        // Swapping the engine out from under an in-progress game would silently reset
        // whatever analysis state the new process starts with mid-position - require
        // disconnecting first.
        ImGui::BeginDisabled(gameSession.IsConnected());
        ImGui::InputText("Engine path", engineExecutablePathBuffer.data(), engineExecutablePathBuffer.size());
        if (ImGui::Button("Restart Engine"))
            StartEngine(controller, engineExecutablePathBuffer.data());
        ImGui::EndDisabled();
        if (gameSession.IsConnected())
            ImGui::TextDisabled("Disconnect to change engine");

        ImGui::Separator();

        int siteIndex = static_cast<int>(selectedSite);
        if (ImGui::Combo("Site", &siteIndex, kSiteNames, IM_ARRAYSIZE(kSiteNames)))
            selectedSite = static_cast<ChessSite>(siteIndex);

        if (!gameSession.IsBrowserRunning())
        {
            if (ImGui::Button("Launch Browser"))
            {
                if (const auto launched = gameSession.LaunchBrowser(ExecutablePathUtil::GetBrowserProfileDirectory()); !launched)
                    LOG_ERROR("Failed to launch browser: {}", launched.error().Message);
                else
                    LOG_INFO("Browser launched - log in and open a game, then click Connect.");
            }
        }
        else
        {
            ImGui::TextWrapped("Log in and open a game in the ChessAssist browser window, then click Connect.");

            // A valid, in-sync session has nothing for another Connect click to do - and
            // clicking it anyway tears down the live CDP connection out from under any
            // in-flight Poll(), which is exactly what was surfacing as "CDP connection lost
            // while waiting for response". Re-connecting is only meaningful to establish a
            // session or to resync after a desync, so that's the only time the button's live.
            ImGui::BeginDisabled(gameSession.IsConnected() && !gameSession.HasDesynced());
            if (ImGui::Button("Connect"))
            {
                if (gameSession.ConnectToSite(selectedSite))
                    LOG_INFO("Connected - watching {}", kSiteNames[static_cast<int>(selectedSite)]);
                else
                    LOG_ERROR("Failed to connect - see Log.");
            }
            ImGui::EndDisabled();

            if (gameSession.IsConnected())
            {
                ImGui::SameLine();
                if (ImGui::Button("Disconnect"))
                {
                    gameSession.Disconnect();
                    LOG_INFO("Disconnected");
                }
            }
        }

        if (gameSession.IsConnected())
        {
            if (gameSession.HasDesynced())
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Tracking lost sync - click Connect to resync");
            else
                ImGui::Text("Connected - watching %s - %zu move(s) recorded", kSiteNames[static_cast<int>(selectedSite)], gameSession.GetTracker().GetMoves().size());
        }
        else
        {
            ImGui::TextDisabled("Not connected - launch the browser and click Connect to start.");
        }

        // Placeholder only - intentionally not wired to anything. Auto-playing moves on the
        // board would take the player out of the loop entirely, which is against the fair-play
        // rules of every site this connects to.
        ImGui::BeginDisabled(true);
        ImGui::Checkbox("Autoplay (not implemented)", &autoplayEnabled);
        ImGui::EndDisabled();

        ImGui::End();

        enginePanel.Draw();
        boardStatePanel.Draw(gameSession.GetTrackedBoard());
        logPanel.Draw();

        const auto now = std::chrono::steady_clock::now();
        if (gameSession.IsConnected() && now - lastPollTime >= kPollInterval)
        {
            lastPollTime = now;

            for (const std::string& move : gameSession.Poll())
                LOG_INFO("Detected move: {}", move);
        }

        window.EndFrame();
    }

    controller.StopSearch();
    controller.Shutdown();
    window.Shutdown();

    ix::uninitNetSystem();

    return 0;
}
