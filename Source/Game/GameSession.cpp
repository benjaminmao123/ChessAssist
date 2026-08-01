#include "GameSession.h"

#include "MoveListDiff.h"

#include "../Engine/EngineController.h"
#include "../Logging/Log.h"

namespace
{
// Our own app-managed Chrome instance's remote-debugging port - distinct from Chrome's
// common default (9222) so we never collide with some other, unrelated debug session the
// user might already have running.
constexpr std::uint16_t kCdpPort = 9333;

template <typename StringRange>
std::string JoinStrings(const StringRange& values, std::string_view separator = ", ")
{
    std::string joined;
    bool first = true;
    for (const auto& value : values)
    {
        if (!first)
            joined += separator;
        joined += value;
        first = false;
    }
    return joined;
}
}  // namespace

GameSession::GameSession(EngineController& controller)
    : m_Controller(&controller)
{
}

std::expected<void, BrowserError> GameSession::LaunchBrowser(const std::filesystem::path& profileDir)
{
    if (m_Launcher.IsRunning())
        return {};

    return m_Launcher.Launch(kCdpPort, profileDir, "about:blank");
}

bool GameSession::IsBrowserRunning() const
{
    return m_Launcher.IsRunning();
}

bool GameSession::ConnectToSite(ChessSite site)
{
    if (!m_Launcher.IsRunning())
    {
        LOG_ERROR("ConnectToSite: browser isn't running - call LaunchBrowser first");
        return false;
    }

    const std::optional<std::string> webSocketUrl = CdpClient::FindPageWebSocketUrl(kCdpPort, ChessSiteAdapter::UrlMatchSubstring(site));
    if (!webSocketUrl)
    {
        LOG_ERROR("ConnectToSite: no open tab found matching '{}' - navigate to the site in the app browser window first", ChessSiteAdapter::UrlMatchSubstring(site));
        return false;
    }

    m_CdpClient.Disconnect();

    if (const std::expected<void, CdpError> connected = m_CdpClient.Connect(*webSocketUrl); !connected)
    {
        LOG_ERROR("ConnectToSite: {}", connected.error().Message);
        return false;
    }

    m_Site = site;
    m_Rules.Reset();
    m_Tracker.Reset();
    m_Connected = true;
    m_Desynced = false;

    return true;
}

bool GameSession::IsConnected() const
{
    return m_Connected;
}

void GameSession::Disconnect()
{
    m_CdpClient.Disconnect();
    m_Connected = false;
    m_Desynced = false;
}

const GameTracker& GameSession::GetTracker() const
{
    return m_Tracker;
}

const BoardState& GameSession::GetTrackedBoard() const
{
    return m_Rules.GetBoard();
}

std::vector<std::string> GameSession::Poll()
{
    std::vector<std::string> newMoves;

    if (!m_Connected)
        return newMoves;

    const std::expected<std::string, CdpError> jsResult = m_CdpClient.EvaluateJs(ChessSiteAdapter::ExtractionScript(m_Site));
    if (!jsResult)
    {
        // Transient - a brief navigation, a page not fully loaded yet, a slow round trip.
        // Don't tear down the connection over one failed poll tick; just retry next tick.
        LOG_WARN("Poll: CDP evaluate failed: {}", jsResult.error().Message);
        return newMoves;
    }

    const std::optional<SiteGameState> state = ChessSiteAdapter::ParseExtractionResult(*jsResult);
    if (!state)
        return newMoves;  // no game currently open on the page - nothing to do yet

    const MoveListDiff diff = ComputeMoveListDiff(state->SanMoves.size(), m_Tracker.GetMoves().size());

    if (diff.Kind != MoveListDiffKind::NoChange)
        LOG_INFO("Poll: read {} move(s) from the page: [{}]", state->SanMoves.size(), JoinStrings(state->SanMoves));

    switch (diff.Kind)
    {
    case MoveListDiffKind::NoChange:
        return newMoves;

    case MoveListDiffKind::AmbiguousShrink:
        // Could be a real reset, could be a flaky/mid-render DOM read. Don't silently
        // discard tracked state (and any in-flight engine analysis) on a guess - require the
        // user to explicitly reconnect.
        LOG_WARN("Poll: move list shrank unexpectedly ({} -> {} moves) - tracking desynced, reconnect to resync", m_Tracker.GetMoves().size(), state->SanMoves.size());
        m_Desynced = true;
        return newMoves;

    case MoveListDiffKind::ResetToFreshGame:
        LOG_INFO("Poll: move list reset to {} move(s) - starting fresh game", state->SanMoves.size());
        m_Rules.Reset();
        m_Tracker.Reset();
        m_Desynced = false;
        break;

    case MoveListDiffKind::Grew:
        break;
    }

    const std::size_t startIndex = (diff.Kind == MoveListDiffKind::ResetToFreshGame) ? 0 : diff.StartIndex;
    for (std::size_t i = startIndex; i < state->SanMoves.size(); ++i)
    {
        const std::optional<std::string> uci = m_Rules.ApplySanMove(state->SanMoves[i]);
        if (!uci)
        {
            LOG_ERROR("Poll: failed to parse SAN move '{}' at index {} - tracking desynced, reconnect to resync", state->SanMoves[i], i);
            m_Desynced = true;
            break;
        }

        LOG_DEBUG("Poll: applied '{}' -> {}", state->SanMoves[i], *uci);
        m_Tracker.RecordMove(*uci);
        newMoves.push_back(*uci);
    }

    if (!newMoves.empty())
        RequestEngineMove();

    return newMoves;
}

bool GameSession::HasDesynced() const
{
    return m_Desynced;
}

void GameSession::RequestEngineMove()
{
    SearchLimits limits;
    limits.MoveTimeMs = 1500;

    LOG_DEBUG("RequestEngineMove: side to move after [{}]", JoinStrings(m_Tracker.GetMoves()));

    (void)m_Controller->FindBestMoveAsync(m_Tracker.GetBaseFen(), limits, m_Tracker.GetMoves());
}
