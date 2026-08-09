#include "GameSession.h"

#include "MoveListDiff.h"

#include "Engine/EngineController.h"
#include "Logging/Log.h"

#include <algorithm>

namespace
{
// Our own app-managed Chrome instance's remote-debugging port - distinct from Chrome's
// common default (9222) so we never collide with some other, unrelated debug session the
// user might already have running.
constexpr std::uint16_t kCdpPort = 9333;

// Blitz mode's fixed search time - short enough that autoplay keeps pace with a fast bot
// rather than falling behind the clock, at the cost of search quality.
constexpr int kBlitzMoveTimeMs = 150;

// Premoving's quick-verify fallback search time (see GameSession::SetPremoveEnabled) - used
// when the opponent didn't play the predicted move, so autoplay still responds fast instead
// of falling back to the full configured Elo/Blitz search length. Deliberately a bit more
// generous than Blitz's own movetime: this path runs on a real, unpredicted position (as
// opposed to blitz mode's every move), so it's worth spending slightly more to reduce the
// chance of a bad quick decision.
constexpr int kPremoveVerifyMoveTimeMs = 300;

// No-Elo-cap default - unchanged from what RequestEngineMove hardcoded before SetEloTarget
// existed.
constexpr int kDefaultMoveTimeMs = 1500;

// Elo-target preset bounds for movetime/depth - interpolated linearly across
// [GameSession::kMinElo, GameSession::kMaxElo]. Not derived from anything Stockfish
// publishes; just a rough "weaker target thinks less" curve, tuned by feel.
constexpr int kPresetMinMoveTimeMs = 100;
constexpr int kPresetMaxMoveTimeMs = 2000;
constexpr int kPresetMinDepth = 4;
constexpr int kPresetMaxDepth = 18;

float NormalizedElo(int elo)
{
    const float t = static_cast<float>(elo - GameSession::kMinElo) / static_cast<float>(GameSession::kMaxElo - GameSession::kMinElo);
    return std::clamp(t, 0.0f, 1.0f);
}

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

std::string_view SideName(PieceColor color)
{
    return color == PieceColor::White ? "White" : "Black";
}
}  // namespace

GameSession::GameSession(EngineController& controller)
    : m_Controller(&controller)
{
}

std::expected<void, BrowserError> GameSession::LaunchBrowser(const std::filesystem::path& profileDir, ChessSite site)
{
    if (m_Launcher.IsRunning())
        return {};

    return m_Launcher.Launch(kCdpPort, profileDir, std::string(ChessSiteAdapter::HomepageUrl(site)));
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
    m_InitialMoveRequested = false;

    LOG_INFO("ConnectToSite: connected to {}", ChessSiteAdapter::UrlMatchSubstring(site));

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

    m_BlackAtBottom = state->PlayingAsBlack;

    const MoveListDiff diff = ComputeMoveListDiff(state->SanMoves.size(), m_Tracker.GetMoves().size());

    if (diff.Kind != MoveListDiffKind::NoChange)
        LOG_INFO("Poll: read {} move(s) from the page: [{}]", state->SanMoves.size(), JoinStrings(state->SanMoves));

    switch (diff.Kind)
    {
    case MoveListDiffKind::NoChange:
        // The page's move-list growing is what normally triggers RequestEngineMove() below -
        // that never fires for a freshly-connected game still at 0 moves (nothing to detect
        // as "new"), so the engine would otherwise never analyze the starting position (and
        // autoplay would never make an opening move) until *something* changed the move
        // count first. Seed it here, once, the first time a poll finds the tracker still
        // empty after connecting.
        if (!m_InitialMoveRequested && m_Tracker.GetMoves().empty())
        {
            LOG_INFO("Poll: still at the starting position after connecting - requesting an initial engine move");
            RequestEngineMove(ShouldQuickVerify());
        }
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
        // Same reason ConnectToSite() clears this: if the reset game is still at 0 moves (the
        // common case - this poll tick usually catches the reset before either side has
        // moved), the loop below won't find any moves to apply and so won't call
        // RequestEngineMove() either, leaving the *next* tick's NoChange branch as the only
        // thing that can seed an initial request for the new game - which it won't, if this
        // flag is still true from the game that just ended. Without this, autoplay and the
        // engine panel silently keep showing the previous game's last position/move/eval
        // until something (a bot's first move, or the user toggling autoplay off and on)
        // happens to trigger a fresh request.
        m_InitialMoveRequested = false;
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

    if (!newMoves.empty() && !TryPremove(newMoves.back()))
        RequestEngineMove(ShouldQuickVerify());

    return newMoves;
}

bool GameSession::HasDesynced() const
{
    return m_Desynced;
}

void GameSession::SetAutoplayEnabled(bool enabled)
{
    // Without this, turning autoplay on only takes effect starting from the *next* detected
    // move: OnEngineBestMove only queues a result from a freshly-started search, and Poll()
    // only starts one when the move list changes - so a result already computed (and
    // discarded, since autoplay was off) for whatever position is on the board right now
    // never gets replayed just because the flag flipped. Re-requesting here re-runs analysis
    // for the current position so autoplay can act on it immediately instead of waiting for
    // the opponent's next move.
    const bool wasEnabled = m_AutoplayEnabled.exchange(enabled);

    if (enabled && !wasEnabled && m_Connected && !m_Desynced)
    {
        LOG_INFO("SetAutoplayEnabled: turned on - requesting a move for the current position");
        RequestEngineMove(ShouldQuickVerify());
    }
}

bool GameSession::IsAutoplayEnabled() const
{
    return m_AutoplayEnabled.load();
}

void GameSession::OnEngineBestMove(const BestMoveResult& result)
{
    // Only for results computed for the tracked player's own turn - see the member comments
    // on m_RequestedForSide/m_BlackAtBottom for why this pairing is safe to read from the
    // reader thread without touching m_Tracker directly.
    const PieceColor myColor = m_BlackAtBottom.load() ? PieceColor::Black : PieceColor::White;
    const PieceColor requestedSide = m_RequestedForSide.load();
    const bool isOurTurn = requestedSide == myColor;

    if (isOurTurn)
    {
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove = result.BestMove;
    }

    if (!m_AutoplayEnabled.load())
    {
        LOG_DEBUG("OnEngineBestMove: '{}' received but autoplay is off", result.BestMove);
        return;
    }

    if (!isOurTurn)
    {
        LOG_INFO("OnEngineBestMove: '{}' was computed for {} to move, but autoplay is controlling {} - not auto-playing", result.BestMove, SideName(requestedSide), SideName(myColor));
        return;
    }

    LOG_INFO("OnEngineBestMove: queuing autoplay of '{}'", result.BestMove);
    std::scoped_lock lock(m_AutoMoveMutex);
    m_PendingAutoMove = result.BestMove;
}

void GameSession::OnEngineInfo(const SearchInfo& info)
{
    // Same "is this analysis actually for our turn" gating as OnEngineBestMove - a premove
    // candidate only makes sense when the PV we're reading is our own search, not one
    // (informational-only) run for the opponent's position.
    const PieceColor myColor = m_BlackAtBottom.load() ? PieceColor::Black : PieceColor::White;
    if (m_RequestedForSide.load() != myColor)
        return;

    // Need at least [ourMove, theirReply, ourNextMove] - shallow early-search PVs that
    // haven't reached that far yet just leave whatever candidate a previous (deeper) info
    // line for this same search already set, rather than clearing it.
    if (info.Pv.size() < 3)
        return;

    std::scoped_lock lock(m_PremoveMutex);
    m_PremoveCandidate = PremoveCandidate{info.Pv[1], info.Pv[2]};
}

void GameSession::Tick()
{
    std::optional<std::string> move;
    {
        std::scoped_lock lock(m_AutoMoveMutex);
        move = std::move(m_PendingAutoMove);
        m_PendingAutoMove.reset();
    }

    if (!move || !m_Connected || m_Desynced)
        return;

    PlayMoveOnBoard(*move);
}

PieceColor GameSession::GetRequestedSide() const
{
    return m_RequestedForSide.load();
}

bool GameSession::IsBlackAtBottom() const
{
    return m_BlackAtBottom.load();
}

std::optional<std::string> GameSession::GetSuggestedMove() const
{
    std::scoped_lock lock(m_SuggestedMoveMutex);
    return m_SuggestedMove;
}

void GameSession::SetEloTarget(std::optional<int> elo)
{
    if (!elo)
    {
        m_MoveTimeMs = kDefaultMoveTimeMs;
        m_SearchDepth.reset();
        return;
    }

    const float t = NormalizedElo(*elo);
    m_MoveTimeMs = kPresetMinMoveTimeMs + static_cast<int>(t * static_cast<float>(kPresetMaxMoveTimeMs - kPresetMinMoveTimeMs));
    m_SearchDepth = kPresetMinDepth + static_cast<int>(t * static_cast<float>(kPresetMaxDepth - kPresetMinDepth));
}

void GameSession::SetBlitzMode(bool enabled)
{
    m_BlitzMode = enabled;
}

bool GameSession::IsBlitzMode() const
{
    return m_BlitzMode;
}

void GameSession::SetPremoveEnabled(bool enabled)
{
    m_PremoveEnabled = enabled;
}

bool GameSession::IsPremoveEnabled() const
{
    return m_PremoveEnabled.load();
}

bool GameSession::TryPremove(const std::string& lastAppliedMove)
{
    if (!m_PremoveEnabled.load() || !m_AutoplayEnabled.load())
        return false;

    // lastAppliedMove needs to actually be the opponent's - i.e. it's now the tracked
    // player's own turn - or this candidate (predicated on it being our search) doesn't apply.
    const PieceColor myColor = m_BlackAtBottom.load() ? PieceColor::Black : PieceColor::White;
    if (m_Tracker.GetSideToMove() != myColor)
        return false;

    std::optional<PremoveCandidate> candidate;
    {
        std::scoped_lock lock(m_PremoveMutex);
        candidate = m_PremoveCandidate;
        m_PremoveCandidate.reset();
    }

    if (!candidate || candidate->PredictedOpponentMove != lastAppliedMove)
        return false;

    LOG_INFO("Poll: premove hit - opponent played the predicted '{}', immediately playing '{}' without waiting for a fresh search", lastAppliedMove, candidate->OurResponse);
    PlayMoveOnBoard(candidate->OurResponse);
    return true;
}

bool GameSession::ShouldQuickVerify() const
{
    return m_PremoveEnabled.load() && m_AutoplayEnabled.load();
}

void GameSession::RequestEngineMove(bool quickVerify)
{
    SearchLimits limits;
    if (m_BlitzMode)
    {
        limits.MoveTimeMs = kBlitzMoveTimeMs;
    }
    else
    {
        limits.MoveTimeMs = m_MoveTimeMs;
        limits.Depth = m_SearchDepth;
    }

    // Premoving missed (or never got a prediction to compare against) - still respond fast
    // rather than waiting out the full configured search. Only ever shortens the search
    // (min, not an override), so this never slows down an already-fast Blitz/low-Elo setup.
    // limits.MoveTimeMs is always set by one of the two branches above.
    if (quickVerify)
        limits.MoveTimeMs = std::min(*limits.MoveTimeMs, kPremoveVerifyMoveTimeMs);

    // The position is about to change (this request supersedes whatever the last suggestion/
    // premove candidate was for) - clear both now rather than leaving stale data around until
    // the new result arrives, which is especially noticeable for the opponent's turn
    // (isOurTurn will be false in OnEngineBestMove/OnEngineInfo, so neither would otherwise
    // get cleared until our turn again).
    {
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove.reset();
    }
    {
        std::scoped_lock lock(m_PremoveMutex);
        m_PremoveCandidate.reset();
    }

    m_RequestedForSide = m_Tracker.GetSideToMove();
    m_InitialMoveRequested = true;

    LOG_INFO("RequestEngineMove: requesting a move for {} after [{}]{}", SideName(m_RequestedForSide.load()), JoinStrings(m_Tracker.GetMoves()), quickVerify ? " (premove quick-verify)" : "");

    (void)m_Controller->FindBestMoveAsync(m_Tracker.GetBaseFen(), limits, m_Tracker.GetMoves());
}

void GameSession::PlayMoveOnBoard(std::string_view uciMove)
{
    if (uciMove.size() < 4)
    {
        LOG_ERROR("PlayMoveOnBoard: malformed UCI move '{}'", uciMove);
        return;
    }

    const std::string_view fromSquare = uciMove.substr(0, 2);
    const std::string_view toSquare = uciMove.substr(2, 2);

    const std::expected<std::string, CdpError> pointsResult = m_CdpClient.EvaluateJs(ChessSiteAdapter::SquareCenterScript(fromSquare, toSquare, m_BlackAtBottom.load()));
    if (!pointsResult)
    {
        LOG_ERROR("PlayMoveOnBoard: failed to locate board squares for '{}': {}", uciMove, pointsResult.error().Message);
        return;
    }

    const std::optional<SquareCenters> centers = ChessSiteAdapter::ParseSquareCenters(*pointsResult);
    if (!centers)
    {
        LOG_ERROR("PlayMoveOnBoard: could not resolve pixel coordinates for '{}' - board element not found", uciMove);
        return;
    }

    if (const std::expected<void, CdpError> dragged = m_CdpClient.DragMouse(centers->From.X, centers->From.Y, centers->To.X, centers->To.Y); !dragged)
    {
        LOG_ERROR("PlayMoveOnBoard: drag failed for '{}': {}", uciMove, dragged.error().Message);
        return;
    }

    LOG_INFO("PlayMoveOnBoard: played {}", uciMove);

    if (uciMove.size() < 5)
        return;

    // Promotion: the drag alone drops the pawn on the back rank, which opens the site's
    // piece-picker rather than completing the move - needs a follow-up click. Best-effort
    // (see ChessSiteAdapter::PromotionPickerScript) - if it can't find the right option, the
    // picker is left open for the user to finish by hand rather than guessing.
    const char promotionLetter = uciMove[4];
    const std::expected<std::string, CdpError> promoResult = m_CdpClient.EvaluateJs(ChessSiteAdapter::PromotionPickerScript(promotionLetter));
    if (!promoResult)
    {
        LOG_WARN("PlayMoveOnBoard: promotion picker lookup failed for '{}': {} - finish the promotion manually", uciMove, promoResult.error().Message);
        return;
    }

    const std::optional<SquarePoint> target = ChessSiteAdapter::ParsePromotionTarget(*promoResult);
    if (!target)
    {
        LOG_WARN("PlayMoveOnBoard: couldn't find a promotion picker option for '{}' - finish the promotion manually", uciMove);
        return;
    }

    if (const std::expected<void, CdpError> clicked = m_CdpClient.Click(target->X, target->Y); !clicked)
        LOG_WARN("PlayMoveOnBoard: promotion click failed for '{}': {} - finish the promotion manually", uciMove, clicked.error().Message);
}
