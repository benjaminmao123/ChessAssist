#include "Engine/EngineController.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/GameSession.h"
#include "UI/AppWindow.h"
#include "UI/BoardViewPanel.h"
#include "UI/CalibrationPanel.h"
#include "UI/EngineInfoPanel.h"
#include "UI/ImGuiLogSink.h"
#include "UI/LogPanel.h"
#include "Vision/ScreenCapture.h"
#include "Vision/VisionTypes.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <imgui.h>

#include <opencv2/core/utils/logger.hpp>

#include <chrono>
#include <ctime>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <system_error>

namespace
{
enum class ChessSite
{
    ChessDotCom,
    Lichess,
};

constexpr const char* kSiteNames[] = {"Chess.com", "Lichess"};

const char* AssetFolderName(ChessSite site)
{
    switch (site)
    {
    case ChessSite::ChessDotCom:
        return "Chessdotcom";
    case ChessSite::Lichess:
        return "Lichess";
    }
    return "Chessdotcom";
}

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

int main()
{
    // Silences OpenCV's own INFO-level logging (e.g. it probing for optional TBB/OpenMP
    // parallel-backend plugin DLLs that vcpkg's build doesn't produce as separate
    // artifacts - harmless, but noisy on every run) without hiding real warnings/errors.
    cv::utils::logging::setLogLevel(cv::utils::logging::LOG_LEVEL_WARNING);

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
    if (!window.Init(1280, 720, "ChessAssist"))
    {
        spdlog::error("Failed to initialize application window");
        return 1;
    }

    EngineInfoPanel enginePanel;
    BoardViewPanel boardPanel;
    CalibrationPanel calibrationPanel;

    ScreenCapture screenCapture;
    boardPanel.UpdateFrame(screenCapture.CaptureRegion(cv::Rect(cv::Point(0, 0), screenCapture.GetScreenSize())));

    EngineController controller;
    if (auto startResult = controller.Start(); !startResult)
    {
        spdlog::error("Failed to start Stockfish: {}", startResult.error().Message);
    }
    else
    {
        controller.SetOnInfo([&enginePanel](const SearchInfo& info) { enginePanel.UpdateInfo(info); });
        controller.SetOnBestMove([&enginePanel](const BestMoveResult& result) { enginePanel.UpdateBestMove(result); });
    }

    GameSession gameSession(controller);

    ChessSite selectedSite = ChessSite::ChessDotCom;

    auto loadTemplatesForSite = [&](ChessSite site) {
        const std::filesystem::path path = ExecutablePathUtil::GetAssetsDirectory() / AssetFolderName(site);
        if (gameSession.LoadPieceTemplates(path))
            spdlog::info("Loaded {} piece templates from {}", kSiteNames[static_cast<int>(site)], path.string());
        else
            spdlog::error("Failed to load {} piece templates from {}", kSiteNames[static_cast<int>(site)], path.string());
    };

    loadTemplatesForSite(selectedSite);

    constexpr std::chrono::milliseconds kPollInterval{500};
    auto lastPollTime = std::chrono::steady_clock::now();

    // Calibration capture is deliberately deferred rather than snapshotting immediately on
    // button click: at the moment of the click, the mouse (and likely window focus) is on
    // ChessAssist's own window, which may be sitting on top of the browser - an immediate
    // capture would grab ChessAssist itself instead of the board underneath it. Countdown
    // gives the user time to switch to the browser; the window is also minimized right
    // before the capture as a backstop in case they forget.
    enum class CaptureState
    {
        Idle,
        CountingDown,
        WaitingForMinimize,
    };

    CaptureState captureState = CaptureState::Idle;
    std::chrono::steady_clock::time_point captureDeadline;

    constexpr std::chrono::milliseconds kCountdownDuration{3000};
    constexpr std::chrono::milliseconds kMinimizeSettleDuration{250};

    while (!window.ShouldClose())
    {
        window.BeginFrame();

        ImGui::Begin("Controls");

        int siteIndex = static_cast<int>(selectedSite);
        if (ImGui::Combo("Site", &siteIndex, kSiteNames, IM_ARRAYSIZE(kSiteNames)))
        {
            selectedSite = static_cast<ChessSite>(siteIndex);
            loadTemplatesForSite(selectedSite);
        }

        if (!gameSession.AreTemplatesLoaded())
        {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s piece templates failed to load - see Log.", kSiteNames[static_cast<int>(selectedSite)]);
        }
        else
        {
            // Piece recognition itself works from the pre-loaded reference templates
            // regardless of position, but move-tracking still assumes the game starts
            // fresh from here (White to move, full castling rights) - full mid-game
            // support (letting you tell it whose turn it is, etc.) isn't wired up yet.
            ImGui::TextWrapped("Calibrate at the start of a new game (White to move, no prior moves).");

            if (captureState == CaptureState::Idle)
            {
                if (ImGui::Button("Calibrate Board"))
                {
                    captureState = CaptureState::CountingDown;
                    captureDeadline = std::chrono::steady_clock::now() + kCountdownDuration;
                }
            }
        }

        if (captureState == CaptureState::CountingDown)
        {
            const double remainingSeconds = std::chrono::duration<double>(captureDeadline - std::chrono::steady_clock::now()).count();
            ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Switch to your browser now - capturing in %.1fs", remainingSeconds > 0.0 ? remainingSeconds : 0.0);
        }
        else if (captureState == CaptureState::WaitingForMinimize)
        {
            ImGui::TextDisabled("Capturing...");
        }

        if (gameSession.IsActive())
            ImGui::Text("Game active - %zu move(s) recorded", gameSession.GetTracker().GetMoves().size());
        else
            ImGui::TextDisabled("No active game - calibrate to start.");

        ImGui::End();

        enginePanel.Draw();
        boardPanel.Draw();
        calibrationPanel.Draw();
        logPanel.Draw();

        if (captureState == CaptureState::CountingDown && std::chrono::steady_clock::now() >= captureDeadline)
        {
            // Get ChessAssist itself out of the shot; give the compositor a moment to
            // actually remove it from the screen before the snapshot below.
            window.Minimize();
            captureState = CaptureState::WaitingForMinimize;
            captureDeadline = std::chrono::steady_clock::now() + kMinimizeSettleDuration;
        }
        else if (captureState == CaptureState::WaitingForMinimize && std::chrono::steady_clock::now() >= captureDeadline)
        {
            const cv::Mat frame = screenCapture.CaptureRegion(cv::Rect(cv::Point(0, 0), screenCapture.GetScreenSize()));
            window.Restore();
            calibrationPanel.Begin(frame, BoardOrientation::WhiteBottom);
            captureState = CaptureState::Idle;
        }

        if (const std::optional<BoardRegion> region = calibrationPanel.TakeResult())
        {
            const cv::Mat frame = screenCapture.CaptureRegion(region->Rect);
            boardPanel.UpdateFrame(frame);

            if (gameSession.StartNewGame(frame, *region))
                spdlog::info("Game started: board calibrated");
            else
                spdlog::error("Failed to start game - piece templates aren't loaded");
        }

        const auto now = std::chrono::steady_clock::now();
        if (gameSession.IsActive() && now - lastPollTime >= kPollInterval)
        {
            lastPollTime = now;

            const cv::Mat frame = screenCapture.CaptureRegion(gameSession.GetRegion().Rect);
            boardPanel.UpdateFrame(frame);

            if (const std::optional<std::string> move = gameSession.Poll(frame))
                spdlog::info("Detected move: {}", *move);
        }

        window.EndFrame();
    }

    controller.StopSearch();
    controller.Shutdown();
    window.Shutdown();

    return 0;
}
