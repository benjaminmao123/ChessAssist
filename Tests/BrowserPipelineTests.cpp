#include "Browser/BrowserLauncher.h"
#include "Browser/CdpClient.h"
#include "Browser/ChessSiteAdapter.h"
#include "Engine/ExecutablePathUtil.h"

#include <ixwebsocket/IXNetSystem.h>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <thread>

// End-to-end coverage for the one thing ChessRulesTests can't reach: does CdpClient +
// ChessSiteAdapter's DOM-extraction JS actually work against a real Chrome, launched by
// BrowserLauncher exactly the way the real app does? Runs against small local HTML fixtures
// (Tests/Fixtures/*.html, copied next to the test binary) over file:// URLs - no live
// internet, login, or site ToS/anti-automation concern, but requires Chrome to be installed
// wherever these tests run.

namespace
{
// main.cpp calls ix::initNetSystem() (WSAStartup on Windows) once at startup - this test
// binary has no equivalent entry point of its own (gtest_main supplies main()), so without
// this, every socket operation CdpClient makes fails silently. A GTest global environment is
// the standard way to run process-wide setup/teardown once regardless of which test runs.
class NetSystemEnvironment : public ::testing::Environment
{
public:
    void SetUp() override { ix::initNetSystem(); }
    void TearDown() override { ix::uninitNetSystem(); }
};

::testing::Environment* const g_netSystemEnvironment = ::testing::AddGlobalTestEnvironment(new NetSystemEnvironment());
}  // namespace

namespace
{
std::string ToFileUrl(const std::filesystem::path& path)
{
    std::string generic = path.generic_string();
    if (!generic.empty() && generic.front() != '/')
        generic = "/" + generic;
    return "file://" + generic;
}

std::optional<SiteGameState> ExtractFromFixture(const char* fixtureFilename, ChessSite site, std::uint16_t port)
{
    const std::filesystem::path fixturePath = ExecutablePathUtil::GetCurrentExecutablePath().parent_path() / "Fixtures" / fixtureFilename;
    if (!std::filesystem::exists(fixturePath))
        return std::nullopt;

    BrowserLauncher launcher;
    const std::filesystem::path profileDir = std::filesystem::temp_directory_path() / ("ChessAssistTestProfile_" + std::to_string(port));
    if (!launcher.Launch(port, profileDir, ToFileUrl(fixturePath)))
        return std::nullopt;

    std::optional<std::string> webSocketUrl;
    for (int attempt = 0; attempt < 20 && !webSocketUrl; ++attempt)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
        webSocketUrl = CdpClient::FindPageWebSocketUrl(port, fixtureFilename);
    }

    if (!webSocketUrl)
    {
        launcher.Terminate();
        return std::nullopt;
    }

    CdpClient client;
    if (!client.Connect(*webSocketUrl))
    {
        launcher.Terminate();
        return std::nullopt;
    }

    std::optional<SiteGameState> state;
    for (int attempt = 0; attempt < 20 && !state; ++attempt)
    {
        const std::expected<std::string, CdpError> result = client.EvaluateJs(ChessSiteAdapter::ExtractionScript(site));
        if (result)
            state = ChessSiteAdapter::ParseExtractionResult(*result);

        if (!state)
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
    }

    launcher.Terminate();
    return state;
}
}  // namespace

TEST(BrowserPipelineTest, ExtractsChessDotComStyleFixture)
{
    const std::optional<SiteGameState> state = ExtractFromFixture("ChessDotComFixture.html", ChessSite::ChessDotCom, 9334);
    ASSERT_TRUE(state.has_value());

    const std::vector<std::string> expected = {"e4", "e5", "Nf3", "Nc6"};
    EXPECT_EQ(state->SanMoves, expected);
}

TEST(BrowserPipelineTest, ExtractsLichessStyleFixtureWithObfuscatedTags)
{
    // Deliberately uses made-up tag names (<zzq1>, <zzq2>) to prove the extraction script
    // really doesn't depend on any specific element name - see ChessSiteAdapter.h for why
    // that matters for lichess's real (per-deploy-regenerated) live-round DOM.
    const std::optional<SiteGameState> state = ExtractFromFixture("LichessFixture.html", ChessSite::Lichess, 9335);
    ASSERT_TRUE(state.has_value());

    const std::vector<std::string> expected = {"e4", "e5", "Nf3", "Nc6"};
    EXPECT_EQ(state->SanMoves, expected);
}

TEST(BrowserPipelineTest, DetectsFreshGameWithNoMovesYetOnChessDotCom)
{
    // Regression coverage: a board with zero moves played must come back as a real (empty)
    // move list, not null - the move-list heuristic alone can't tell "zero moves" apart from
    // "no game open" (see ChessSiteAdapter.cpp's isGameOpen()). Returning null here was the
    // bug that left GameSession's Poll() unable to ever request an opening engine move.
    const std::optional<SiteGameState> state = ExtractFromFixture("ChessDotComFreshGameFixture.html", ChessSite::ChessDotCom, 9336);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->SanMoves.empty());
}

TEST(BrowserPipelineTest, DetectsFreshGameWithNoMovesYetOnLichess)
{
    const std::optional<SiteGameState> state = ExtractFromFixture("LichessFreshGameFixture.html", ChessSite::Lichess, 9337);
    ASSERT_TRUE(state.has_value());
    EXPECT_TRUE(state->SanMoves.empty());
}
