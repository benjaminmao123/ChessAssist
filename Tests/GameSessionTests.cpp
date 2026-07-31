#include "SyntheticBoard.h"

#include "Engine/EngineController.h"
#include "Engine/ExecutablePathUtil.h"
#include "Game/GameSession.h"
#include "Vision/VisionTypes.h"

#include <gtest/gtest.h>

#include <chrono>

TEST(GameSessionTest, StartNewGameReturnsPromptlyAgainstRealEngine)
{
    EngineController controller;
    ASSERT_TRUE(controller.Start().has_value());

    GameSession session(controller);
    ASSERT_TRUE(session.LoadPieceTemplates(ExecutablePathUtil::GetAssetsDirectory() / "Chessdotcom"));

    const cv::Mat frame = SyntheticBoard::BuildStartingPositionFrame();
    const BoardRegion region{cv::Rect(0, 0, SyntheticBoard::kBoardSize, SyntheticBoard::kBoardSize), BoardOrientation::WhiteBottom};

    const auto startTime = std::chrono::steady_clock::now();
    const bool started = session.StartNewGame(frame, region);
    const auto elapsed = std::chrono::steady_clock::now() - startTime;

    EXPECT_TRUE(started);
    EXPECT_LT(elapsed, std::chrono::seconds(2)) << "StartNewGame took " << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() << "ms - should return almost immediately (engine search is async)";

    controller.Shutdown();
}
