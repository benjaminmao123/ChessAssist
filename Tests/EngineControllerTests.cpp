#include "Engine/EngineController.h"
#include "Vision/VisionTypes.h"

#include <gtest/gtest.h>

// Spawns the real Stockfish binary, unlike the other test files which are pure/synthetic.
// Requires stockfish[.exe] to sit next to the test executable (see Tests/CMakeLists.txt's
// post-build copy step).
TEST(EngineControllerTest, FindsBestMoveForStartingPosition)
{
    EngineController controller;
    const auto startResult = controller.Start();
    ASSERT_TRUE(startResult.has_value()) << startResult.error().Message;

    SearchLimits limits;
    limits.MoveTimeMs = 300;

    const auto result = controller.FindBestMove(kStandardStartFen, limits);
    ASSERT_TRUE(result.has_value()) << result.error().Message;
    EXPECT_FALSE(result->BestMove.empty());

    controller.Shutdown();
}

TEST(EngineControllerTest, StreamsSearchInfoWhileThinking)
{
    EngineController controller;
    ASSERT_TRUE(controller.Start().has_value());

    int depthUpdates = 0;
    controller.SetOnInfo([&depthUpdates](const SearchInfo&) { ++depthUpdates; });

    SearchLimits limits;
    limits.MoveTimeMs = 300;

    const auto result = controller.FindBestMove(kStandardStartFen, limits);
    ASSERT_TRUE(result.has_value());
    EXPECT_GT(depthUpdates, 0);

    controller.Shutdown();
}
