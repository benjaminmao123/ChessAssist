#include "GameSession.h"

#include "MoveListDiff.h"

#include "Chess/MoveGenerator.h"
#include "Engine/EngineController.h"
#include "Logging/Log.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <thread>

namespace
{
// Our own app-managed Chrome instance's remote-debugging port - distinct from Chrome's
// common default (9222) so we never collide with some other, unrelated debug session the
// user might already have running.
constexpr std::uint16_t kCdpPort = 9333;

// Blitz mode's fixed search time - short enough that autoplay keeps pace with a fast bot
// rather than falling behind the clock, at the cost of search quality.
constexpr int kBlitzMoveTimeMs = 150;

// Premoving's quick-verify fallback search time (see GameSession::SetPremoveEnabled) - a bit
// more generous than Blitz's own movetime, since this path runs on a real, unpredicted position
// rather than every move, so it's worth spending slightly more to reduce a bad quick decision.
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

// Approximates a mate score as a large equivalent centipawn value (closer mates are more
// extreme in either direction) so GetAccuracyPercent's centipawn-loss math can treat mate and
// plain cp scores uniformly instead of needing a separate case for each.
float ScoreToCentipawns(const SearchInfo& info)
{
    if (info.ScoreMate)
    {
        constexpr float kMateEquivalentCp = 10000.0f;
        const float mate = static_cast<float>(*info.ScoreMate);
        // "mate 0" means this position already IS checkmate against the side to move - the
        // worst outcome for them, not a win. Only mate > 0 is a win, so mate == 0 must fall into
        // the same "loss" branch as negative (mated-in-N) values, or the move that delivered
        // mate would get scored as a blunder instead of perfect.
        return mate > 0.0f ? (kMateEquivalentCp - mate) : (-kMateEquivalentCp - mate);
    }
    return static_cast<float>(info.ScoreCp.value_or(0));
}

// Picks the artificial pre-move delay for a freshly queued autoplay move (see
// GameSession::SetMoveDelay) - a fresh draw per call since this only needs to look unpredictable,
// not be reproducible.
int RandomMoveDelayMs(int minMs, int maxMs)
{
    if (maxMs <= minMs)
        return minMs;

    thread_local std::mt19937 rng{std::random_device{}()};
    return std::uniform_int_distribution<int>(minMs, maxMs)(rng);
}
}  // namespace

GameSession::GameSession(EngineController& controller)
    : m_Controller(&controller)
{
}

PieceColor GameSession::MyColor() const
{
    return m_BlackAtBottom.load() ? PieceColor::Black : PieceColor::White;
}

void GameSession::ConfigureMultiPv(EngineController& controller)
{
    if (kMultiPvLines > 1)
        controller.SetOption("MultiPV", std::to_string(kMultiPvLines));
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
    ++m_PositionGeneration;
    ResetAccuracy();

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

std::optional<int> GameSession::GetCheckedKingSquare() const
{
    return m_Rules.CheckedKingSquare();
}

CastlingRights GameSession::GetCastlingRights() const
{
    return m_Rules.GetCastlingRights();
}

std::optional<int> GameSession::GetEnPassantTarget() const
{
    return m_Rules.GetEnPassantTarget();
}

std::uint64_t GameSession::GetPositionGeneration() const
{
    return m_PositionGeneration.load();
}

std::vector<std::string> GameSession::Poll()
{
    std::vector<std::string> newMoves;

    if (!m_Connected)
        return newMoves;

    const std::expected<std::string, CdpError> jsResult = m_CdpClient.EvaluateJs(ChessSiteAdapter::ExtractionScript(m_Site));
    if (!jsResult)
    {
        if (!m_CdpClient.IsConnected())
        {
            // The browser (or the watched tab) closed out from under us - the WebSocket is gone
            // for good, so retrying every poll tick would just spam this failure. Relaunching
            // the browser alone wouldn't fix it either (that only spawns a fresh Chrome
            // process/CDP endpoint, not a reconnect), so reset to the same state a manual
            // Disconnect leaves, letting ConnectToSite() cleanly re-establish later.
            LOG_WARN("Poll: CDP connection lost - resetting to disconnected");
            Disconnect();
            return newMoves;
        }

        // Transient and the connection itself is still alive - a slow round trip, a JS
        // exception, a malformed response. Don't tear down over one failed tick; just retry.
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
        // RequestEngineMove() below is normally triggered by the move list growing, which never
        // fires for a freshly-connected game still at 0 moves - so seed it once here, the first
        // time a poll finds the tracker still empty after connecting. (ResetToFreshGame seeds
        // its own zero-move case immediately, so this is purely the fresh-connection fallback.)
        if (!m_InitialMoveRequested && m_Tracker.GetMoves().empty())
        {
            LOG_INFO("Poll: still at the starting position after connecting - requesting an initial engine move");
            RequestEngineMove(ShouldQuickVerify());
        }
        return newMoves;

    case MoveListDiffKind::AmbiguousShrink:
        // Could be a real reset or a flaky/mid-render DOM read - don't discard tracked state
        // on a guess; require the user to explicitly reconnect.
        LOG_WARN("Poll: move list shrank unexpectedly ({} -> {} moves) - tracking desynced, reconnect to resync", m_Tracker.GetMoves().size(), state->SanMoves.size());
        m_Desynced = true;
        return newMoves;

    case MoveListDiffKind::ResetToFreshGame:
        LOG_INFO("Poll: move list reset to {} move(s) - starting fresh game", state->SanMoves.size());
        m_Rules.Reset();
        m_Tracker.Reset();
        m_Desynced = false;
        m_InitialMoveRequested = false;
        ResetAccuracy();
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
    {
        if (!TryPremove(newMoves.back()))
            RequestEngineMove(ShouldQuickVerify());
    }
    else if (diff.Kind == MoveListDiffKind::ResetToFreshGame && m_Tracker.GetMoves().empty())
    {
        // A reset straight to 0 moves has no opening move for the loop above to apply, so
        // nothing requested a move for the new game yet. Seed it here immediately rather than
        // deferring to the NoChange branch's fallback, which isn't guaranteed to run on the very
        // next tick - until it does, autoplay and the engine panel would keep silently showing
        // the previous game's stale position/move/eval.
        LOG_INFO("Poll: fresh game has no moves yet - requesting an initial engine move");
        RequestEngineMove(ShouldQuickVerify());
    }

    // A real move landed, or the game reset (even to 0 moves) - see GetPositionGeneration()'s
    // comment for why NoChange/AmbiguousShrink don't bump this.
    if (!newMoves.empty() || diff.Kind == MoveListDiffKind::ResetToFreshGame)
        ++m_PositionGeneration;

    return newMoves;
}

bool GameSession::HasDesynced() const
{
    return m_Desynced;
}

void GameSession::SetAutoplayEnabled(bool enabled)
{
    // Without this, turning autoplay on only takes effect from the *next* detected move:
    // OnEngineBestMove only queues a result from a freshly-started search, and Poll() only
    // starts one when the move list changes. Re-requesting here re-runs analysis for the current
    // position so autoplay can act immediately instead of waiting for the opponent's next move.
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

void GameSession::SetMoveDelay(int minMs, int maxMs)
{
    m_MinMoveDelayMs = minMs;
    m_MaxMoveDelayMs = std::max(minMs, maxMs);
}

void GameSession::QueueAutoplayMove(const std::string& uciMove)
{
    if (!m_AutoplayEnabled.load())
        return;

    const int delayMs = RandomMoveDelayMs(m_MinMoveDelayMs.load(), m_MaxMoveDelayMs.load());

    LOG_INFO("QueueAutoplayMove: queuing autoplay of '{}' (playing in {} ms)", uciMove, delayMs);
    std::scoped_lock lock(m_AutoMoveMutex);
    m_PendingAutoMove = uciMove;
    m_AutoMoveReadyTime = std::chrono::steady_clock::now() + std::chrono::milliseconds(delayMs);
}

void GameSession::PlayBestMoveNow()
{
    if (!m_Connected || m_Desynced)
    {
        LOG_WARN("PlayBestMoveNow: not connected or desynced - ignoring");
        return;
    }

    const std::optional<std::string> move = GetSuggestedMove();
    if (!move)
    {
        LOG_WARN("PlayBestMoveNow: no current suggestion to play");
        return;
    }

    LOG_INFO("PlayBestMoveNow: manually playing '{}'", *move);
    PlayMoveOnBoard(*move);
}

void GameSession::OnEngineBestMove(const BestMoveResult& result)
{
    // This search only ran to populate display/accuracy side effects (see m_CosmeticSearch's
    // comment) - the opening book already decided this turn's move, so this result must be
    // discarded rather than overwriting m_SuggestedMove or queuing a second autoplay move.
    if (m_CosmeticSearch.load())
    {
        LOG_DEBUG("OnEngineBestMove: '{}' discarded - cosmetic search for a book-decided move", result.BestMove);
        return;
    }

    // See the member comments on m_RequestedForSide/m_BlackAtBottom for why this pairing is
    // safe to read from the reader thread without touching m_Tracker directly.
    const PieceColor myColor = MyColor();
    const PieceColor requestedSide = m_RequestedForSide.load();
    const bool isOurTurn = requestedSide == myColor;

    // Shown as the board arrow regardless of whose turn this was analyzing - unlike
    // m_PendingAutoMove below, which stays isOurTurn-gated since playing a move for the
    // opponent would be nonsensical.
    {
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove = result.BestMove;
    }

    if (!isOurTurn)
    {
        // This search analyzed the position right after whatever move the tracked player just
        // made (autoplay, a premove, or a manual move - doesn't matter which). If there's a
        // pending "before" eval, this is the "after" search that scores that move - see
        // GetAccuracyPercent()'s comment. Independent of m_AutoplayEnabled below: accuracy
        // tracks whatever actually got played.
        if (const std::optional<AccuracyTracker::MoveScore> score = m_Accuracy.TryScoreMove())
            LOG_INFO("OnEngineBestMove: scored the tracked player's last move at {:.1f}% accuracy (avg {:.1f}% over {} move(s))", score->MoveAccuracyPercent, score->RunningAveragePercent, score->MoveCount);
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

    QueueAutoplayMove(result.BestMove);
}

void GameSession::OnEngineInfo(const SearchInfo& info)
{
    // Same "is this analysis actually for our turn" gating as OnEngineBestMove.
    const PieceColor myColor = MyColor();
    const bool isOurTurn = m_RequestedForSide.load() == myColor;

    // Alternate-line candidate moves - collected regardless of whose turn this search is
    // analyzing, same scope as m_SuggestedMove. Independent of everything below, which (accuracy
    // tracking, the premove candidate) must only look at the primary (multipv 1) line - a
    // multipv>=2 line is a deliberately weaker alternative, not a real read on the position.
    m_AlternateMoves.OnInfo(info);

    if (info.MultiPvIndex != 1)
        return;

    if (info.ScoreCp || info.ScoreMate)
    {
        // Accuracy tracking: perspectiveCp is always "how good for the tracked player" -
        // continuously overwritten as the search deepens ("last update wins"), paired off in
        // OnEngineBestMove once the relevant search completes. See GetAccuracyPercent()'s
        // comment for the full scheme.
        const float perspectiveCp = ScoreToCentipawns(info) * (isOurTurn ? 1.0f : -1.0f);
        if (isOurTurn)
            m_Accuracy.RecordBeforeEval(perspectiveCp);
        else
            m_Accuracy.RecordAfterEval(perspectiveCp);
    }

    // A premove candidate only makes sense when the PV we're reading is our own search, not
    // one (informational-only) run for the opponent's position.
    if (!isOurTurn)
        return;

    // Need at least [ourMove, theirReply, ourNextMove] - shallow early-search PVs that haven't
    // reached that far yet just leave whatever candidate a previous, deeper info line already
    // set.
    if (info.Pv.size() < 3)
        return;

    m_Premove.Update(info.Pv[0], info.Pv[1], info.Pv[2], m_PositionGeneration.load());
}

void GameSession::Tick()
{
    std::optional<std::string> move;
    {
        std::scoped_lock lock(m_AutoMoveMutex);
        if (m_PendingAutoMove && std::chrono::steady_clock::now() >= m_AutoMoveReadyTime)
        {
            move = std::move(m_PendingAutoMove);
            m_PendingAutoMove.reset();
        }
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

std::optional<std::string> GameSession::GetLookaheadMove() const
{
    const PieceColor myColor = MyColor();
    const bool isOurTurn = m_Tracker.GetSideToMove() == myColor;

    // Resolved before touching m_PremoveMutex, not nested within its lock - GetSuggestedMove()
    // takes m_SuggestedMoveMutex itself, and this keeps the two mutexes from ever needing to be
    // held at the same time.
    const std::optional<std::string> anchor = isOurTurn ? GetSuggestedMove() : (m_Tracker.GetMoves().empty() ? std::nullopt : std::make_optional(m_Tracker.GetMoves().back()));
    if (!anchor)
        return std::nullopt;

    const std::optional<PremoveTracker::Candidate> candidate = m_Premove.Peek();
    if (!candidate || candidate->ExpectedOwnMove != *anchor)
        return std::nullopt;  // stale - the candidate no longer describes the current anchor

    // Belt-and-braces against ExpectedOwnMove's string coincidentally matching the anchor again
    // at a *different* point in the game (the string check above can't tell those apart): on our
    // own turn the candidate must have been computed for the position still on the board right
    // now; on the opponent's turn, exactly one real move (ours) has landed since.
    const std::uint64_t currentGeneration = m_PositionGeneration.load();
    const std::uint64_t expectedGeneration = isOurTurn ? currentGeneration : currentGeneration - 1;
    if (candidate->Generation != expectedGeneration)
        return std::nullopt;

    // On our own turn, the board still shows the position *before* our move (ExpectedOwnMove),
    // so the lookahead (PredictedOpponentMove) is only legal one ply beyond what's displayed. On
    // the opponent's turn, our move already happened for real (reflected in m_Rules), so only
    // their predicted reply is still the missing ply before OurResponse becomes legal.
    const std::string& intermediateMove = isOurTurn ? candidate->ExpectedOwnMove : candidate->PredictedOpponentMove;
    const std::string& lookaheadMove = isOurTurn ? candidate->PredictedOpponentMove : candidate->OurResponse;

    // Validate against a position with the intermediate move actually applied, rather than
    // trusting the string and drawing it straight onto the current board - a pawn move
    // (especially en passant) can otherwise look outright illegal: e.g. PredictedOpponentMove
    // capturing en passant a pawn that only arrives via our own not-yet-played ExpectedOwnMove
    // would draw a "capture" on a square that's genuinely empty on the board as displayed now.
    const MoveGenerator::PositionState position{m_Rules.GetBoard(), m_Rules.GetSideToMove(), m_Rules.GetCastlingRights(), m_Rules.GetEnPassantTarget()};
    if (!MoveGenerator::VerifyTwoPlyContinuation(position, intermediateMove, lookaheadMove))
        return std::nullopt;  // shouldn't happen (it's the engine's own PV/our actual last move), but never draw an unverified arrow

    return lookaheadMove;
}

std::vector<std::string> GameSession::GetAlternateMoves() const
{
    return m_AlternateMoves.GetMoves();
}

std::optional<float> GameSession::GetAccuracyPercent() const
{
    return m_Accuracy.GetPercent();
}

void GameSession::ResetAccuracy()
{
    m_Accuracy.Reset();
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

bool GameSession::LoadOpeningBook(const std::filesystem::path& path)
{
    return m_OpeningBook.Load(path);
}

bool GameSession::HasOpeningBookLoaded() const
{
    return m_OpeningBook.IsLoaded();
}

void GameSession::SetOpeningBookEnabled(bool enabled)
{
    m_OpeningBookEnabled = enabled;
}

bool GameSession::IsOpeningBookEnabled() const
{
    return m_OpeningBookEnabled;
}

void GameSession::SetBookSelectionMode(PolyglotBook::SelectionMode mode)
{
    m_BookSelectionMode = mode;
}

bool GameSession::TryPremove(const std::string& lastAppliedMove)
{
    if (!m_PremoveEnabled.load() || !m_AutoplayEnabled.load())
        return false;

    const PieceColor myColor = MyColor();
    if (m_Tracker.GetSideToMove() != myColor)
    {
        // lastAppliedMove was our own move (side to move just flipped to the opponent) - too
        // early to check the candidate against the opponent's reply, but this is the moment we
        // can validate it's still trustworthy: if we didn't actually play the move the candidate
        // assumed (e.g. a human overrode the suggestion), discard it rather than risk it later
        // matching the opponent's move by coincidence and firing a response computed for a
        // different position.
        m_Premove.InvalidateIfMismatched(lastAppliedMove);
        return false;
    }

    const std::optional<PremoveTracker::Candidate> candidate = m_Premove.Take();

    if (!candidate || candidate->PredictedOpponentMove != lastAppliedMove)
        return false;

    LOG_INFO("Poll: premove hit - opponent played the predicted '{}', immediately playing '{}' without waiting for a fresh search", lastAppliedMove, candidate->OurResponse);
    PlayMoveOnBoard(candidate->OurResponse);

    // No fresh "before" search ran for this move (that's the point of a premove hit - playing
    // instantly rather than waiting one out), so any pending "before" eval is stale, left over
    // from an earlier, unrelated position. Left alone, it would get incorrectly paired with the
    // next "after" eval - a bogus accuracy score for a move that was never evaluated. Clearing
    // both ensures this move is skipped, per GetAccuracyPercent()'s documented intent.
    m_Accuracy.ClearPendingEvals();

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

    // Premoving missed (or never got a prediction) - still respond fast rather than waiting out
    // the full configured search. Only ever shortens the search (min, not an override), so this
    // never slows down an already-fast Blitz/low-Elo setup.
    if (quickVerify)
        limits.MoveTimeMs = std::min(*limits.MoveTimeMs, kPremoveVerifyMoveTimeMs);

    // The position is about to change - clear the suggestion now rather than leaving stale data
    // around until the new result arrives (especially noticeable on the opponent's turn, since
    // OnEngineBestMove wouldn't otherwise clear it until our turn again).
    //
    // m_PremoveCandidate is deliberately NOT cleared here - see its comment in GameSession.h.
    // TryPremove() owns clearing it.
    {
        std::scoped_lock lock(m_SuggestedMoveMutex);
        m_SuggestedMove.reset();
    }
    m_AlternateMoves.Clear();

    m_RequestedForSide = m_Tracker.GetSideToMove();
    m_InitialMoveRequested = true;

    // Opening book: only ever intercepts a request for the tracked player's own turn - a request
    // to evaluate the opponent's position, or to score our last move for accuracy tracking,
    // always goes to the engine regardless of whether the book is on. A book hit publishes its
    // move immediately, the same way OnEngineBestMove would (see QueueAutoplayMove), so the
    // on-board arrow, "play now" hotkey, and artificial delay all keep working.
    const PieceColor myColor = MyColor();
    bool wasBookMove = false;
    if (m_RequestedForSide.load() == myColor && m_OpeningBookEnabled && m_OpeningBook.IsLoaded())
    {
        const std::optional<std::string> bookMove = m_OpeningBook.FindMove(m_Rules.GetBoard(), m_Rules.GetSideToMove(), m_Rules.GetCastlingRights(), m_Rules.GetEnPassantTarget(), m_BookSelectionMode);
        if (bookMove)
        {
            LOG_INFO("RequestEngineMove: playing book move '{}' for {} after [{}]", *bookMove, SideName(myColor), JoinStrings(m_Tracker.GetMoves()));
            {
                std::scoped_lock lock(m_SuggestedMoveMutex);
                m_SuggestedMove = *bookMove;
            }
            QueueAutoplayMove(*bookMove);
            wasBookMove = true;
        }
    }

    // The book move (if any) is already decided and published above - this search is purely
    // cosmetic, feeding GetLookaheadMove()/GetAlternateMoves() (and accuracy's "before" eval)
    // with real PV/score data instead of leaving them empty while the position stays in book.
    // OnEngineBestMove checks m_CosmeticSearch and discards this search's result rather than
    // overwriting m_SuggestedMove or queuing a second autoplay move - if the engine's own top
    // choice doesn't match the book's, GetLookaheadMove()'s freshness check already handles that
    // gracefully by just not showing a lookahead arrow.
    m_CosmeticSearch = wasBookMove;

    LOG_INFO("RequestEngineMove: requesting {}a move for {} after [{}]{}", wasBookMove ? "a cosmetic (book move already decided) " : "", SideName(m_RequestedForSide.load()), JoinStrings(m_Tracker.GetMoves()),
             quickVerify ? " (premove quick-verify)" : "");

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
    // piece-picker rather than completing the move - needs a follow-up click. Best-effort: if
    // it can't find the right option, the picker is left open for the user to finish by hand.
    const char promotionLetter = uciMove[4];
    // Rank 8 (uciMove[3] == '8') is always White promoting, rank 1 always Black, regardless of
    // which side is tracked or how the board is oriented.
    const char promotingColor = (uciMove[3] == '8') ? 'w' : 'b';
    const std::string promotionScript = ChessSiteAdapter::PromotionPickerScript(promotionLetter, promotingColor);

    // The picker isn't necessarily in the DOM the instant the drag's mouseup fires - some sites
    // (lichess included) insert it a render pass or two later. Retry briefly instead of giving
    // up on the first empty result. A genuine CDP/JS failure (vs. "ran fine, nothing there yet")
    // still fails fast below rather than retrying pointlessly.
    std::optional<SquarePoint> target;
    for (int attempt = 0; attempt < 10 && !target; ++attempt)
    {
        if (attempt > 0)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

        const std::expected<std::string, CdpError> promoResult = m_CdpClient.EvaluateJs(promotionScript);
        if (!promoResult)
        {
            LOG_WARN("PlayMoveOnBoard: promotion picker lookup failed for '{}': {} - finish the promotion manually", uciMove, promoResult.error().Message);
            return;
        }

        target = ChessSiteAdapter::ParsePromotionTarget(*promoResult);
    }

    if (!target)
    {
        LOG_WARN("PlayMoveOnBoard: couldn't find a promotion picker option for '{}' - finish the promotion manually", uciMove);
        return;
    }

    if (const std::expected<void, CdpError> clicked = m_CdpClient.Click(target->X, target->Y); !clicked)
        LOG_WARN("PlayMoveOnBoard: promotion click failed for '{}': {} - finish the promotion manually", uciMove, clicked.error().Message);
}
