#include "Engine/EngineController.h"
#include "UI/AppWindow.h"
#include "UI/BoardViewPanel.h"
#include "UI/CalibrationPanel.h"
#include "UI/EngineInfoPanel.h"
#include "Vision/ScreenCapture.h"
#include "Vision/VisionTypes.h"

#include <spdlog/spdlog.h>

#include <imgui.h>

#include <optional>

int main()
{
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

        // Live analysis of the starting position streams into EngineInfoPanel while the
        // window loop below runs; StopSearch()/Shutdown() below stop it on exit.
        SearchLimits limits;
        limits.Infinite = true;
        (void)controller.FindBestMoveAsync(kStandardStartFen, limits);
    }

    while (!window.ShouldClose())
    {
        window.BeginFrame();

        ImGui::Begin("Controls");
        if (ImGui::Button("Calibrate Board"))
        {
            const cv::Mat frame = screenCapture.CaptureRegion(cv::Rect(cv::Point(0, 0), screenCapture.GetScreenSize()));
            calibrationPanel.Begin(frame, BoardOrientation::WhiteBottom);
        }
        ImGui::End();

        enginePanel.Draw();
        boardPanel.Draw();
        calibrationPanel.Draw();

        if (const std::optional<BoardRegion> region = calibrationPanel.TakeResult())
        {
            spdlog::info("Board calibrated: x={} y={} w={} h={}", region->Rect.x, region->Rect.y, region->Rect.width, region->Rect.height);
            boardPanel.UpdateFrame(screenCapture.CaptureRegion(region->Rect));
        }

        window.EndFrame();
    }

    controller.StopSearch();
    controller.Shutdown();
    window.Shutdown();

    return 0;
}
