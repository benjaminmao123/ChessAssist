#pragma once

#include "AccuracyTracker.h"
#include "GameTracker.h"
#include "PremoveTracker.h"

#include "Browser/BrowserLauncher.h"
#include "Browser/CdpClient.h"
#include "Browser/ChessSiteAdapter.h"
#include "Chess/ChessRules.h"
#include "Chess/PolyglotBook.h"
#include "Engine/EngineTypes.h"
#include "Engine/MultiPvCollector.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class EngineController;

// Orchestrates a live game: launches and owns a dedicated Chrome instance (BrowserLauncher),
// polls the chess site's move-list DOM through it (CdpClient + ChessSiteAdapter), converts
// newly-seen SAN moves to UCI (ChessRules), and feeds them into GameTracker/EngineController.
class GameSession
{
public:
    explicit GameSession(EngineController& controller);

    // One-time per app session: launches (or no-ops if already running) the app-managed
    // Chrome instance with CDP remote debugging enabled, navigated directly to the site's
    // homepage rather than a blank tab.
    [[nodiscard]] std::expected<void, BrowserError> LaunchBrowser(const std::filesystem::path& profileDir, ChessSite site);
    [[nodiscard]] bool IsBrowserRunning() const;

    // Finds site's already-open tab in the app-managed Chrome, connects, and resets tracking -
    // the next Poll() baselines from whatever moves already exist in the site's move list (0 for
    // a fresh game, N for joining mid-game, handled by Poll's diff logic).
    [[nodiscard]] bool ConnectToSite(ChessSite site);
    [[nodiscard]] bool IsConnected() const;

    // Ends the current session - closes the CDP connection and clears tracked state. Safe to
    // call even if not connected.
    void Disconnect();

    [[nodiscard]] const GameTracker& GetTracker() const;
    [[nodiscard]] const BoardState& GetTrackedBoard() const;

    // Forwarders onto m_Rules - SandboxSession needs the full position (not just the board) to
    // seed its own hypothetical line. UI-thread-only, like GetTrackedBoard().
    [[nodiscard]] CastlingRights GetCastlingRights() const;
    [[nodiscard]] std::optional<int> GetEnPassantTarget() const;

    // Bumped by Poll() whenever it applied >=1 real move or reset to a fresh game (even at 0
    // moves), and by ConnectToSite(). A cheap "has the live position changed" signal callers
    // compare against their own last-seen value to know when to resync derived state (e.g.
    // SandboxSession) - move-count/FEN alone can't distinguish two zero-move resets in a row
    // from "nothing happened." UI-thread-only, like m_Rules/m_Tracker.
    [[nodiscard]] std::uint64_t GetPositionGeneration() const;

    // Square index of the tracked board's checked king, if either side is in check, else
    // nullopt. UI-thread-only, same as GetTrackedBoard(). Used by BoardStatePanel to highlight
    // the checked king.
    [[nodiscard]] std::optional<int> GetCheckedKingSquare() const;

    // Re-runs the site's extraction script and applies any moves new since the last call.
    // Returns them, in order, as UCI. Requests a fresh engine move once at the end if anything
    // was applied - never more than once per call.
    [[nodiscard]] std::vector<std::string> Poll();

    // True once Poll() found the site's move list shrink in a way that isn't a clean new-game
    // reset, or a SAN move failed to parse - tracking can't be trusted until ConnectToSite() is
    // called again.
    [[nodiscard]] bool HasDesynced() const;

    // When enabled, an engine best-move result for the tracked player's own turn (inferred from
    // board orientation - see m_BlackAtBottom) is automatically played. Turning it on
    // re-requests a move for the current position so autoplay acts immediately rather than
    // waiting for the next detected move. Call from the UI thread only.
    void SetAutoplayEnabled(bool enabled);
    [[nodiscard]] bool IsAutoplayEnabled() const;

    // Sets an artificial delay applied before Tick() plays a queued autoplay move, layered on
    // top of the engine's own think time - a pacing/human-likeness knob only, doesn't affect
    // analysis. A fresh random delay in [minMs, maxMs] is picked each time a move is queued;
    // maxMs is clamped up to minMs if given smaller. Doesn't affect the premove fast-path
    // (TryPremove), which is deliberately instant. Call from the UI thread only.
    void SetMoveDelay(int minMs, int maxMs);

    // Plays the current engine suggestion (see GetSuggestedMove()) immediately - bypasses both
    // autoplay's turn-gating and SetMoveDelay's delay. Intended for a manual "play now" hotkey
    // when autoplay is off. No-op (logged) if not connected, desynced, or there's no current
    // suggestion. Call from the UI thread only.
    void PlayBestMoveNow();

    // Routed here from EngineController::SetOnBestMove via main.cpp (which also forwards to
    // EngineInfoPanel, since EngineController supports only one subscriber). Called from the
    // engine's background reader thread: must not touch m_CdpClient/m_Rules/m_Tracker directly -
    // only stashes the move for Tick() to play.
    void OnEngineBestMove(const BestMoveResult& result);

    // Routed here from EngineController::SetOnInfo via App (which also forwards to
    // EngineInfoPanel) - feeds SetPremoveEnabled's premove detection. Called from the engine's
    // background reader thread - same constraints as OnEngineBestMove.
    void OnEngineInfo(const SearchInfo& info);

    // Plays a pending autoplay move (queued by OnEngineBestMove), if any and its configured
    // delay (see SetMoveDelay) has elapsed. Call once per frame from the UI thread, alongside
    // Poll().
    void Tick();

    // Side to move that the most recently requested engine search is analyzing - safe to call
    // from any thread. UCI "score" is always relative to this side, not White, so display code
    // needs it to flip the sign into a White-perspective convention when Black is to move.
    [[nodiscard]] PieceColor GetRequestedSide() const;

    // True if the tracked player's pieces render at the bottom of the site's board (i.e. the
    // player is Black) - see m_BlackAtBottom. Safe to call from any thread. Used by
    // BoardStatePanel to draw the tracked board in the live game's orientation.
    [[nodiscard]] bool IsBlackAtBottom() const;

    // The engine's suggestion for the tracked player's own turn, as UCI (e.g. "e2e4") - nullopt
    // if none yet, it's currently the opponent's turn, or the position has moved on since the
    // last suggestion (RequestEngineMove() clears this before every new search). Safe to call
    // from the UI thread. Drawn by BoardStatePanel as an on-board arrow.
    [[nodiscard]] std::optional<std::string> GetSuggestedMove() const;

    // The other side's anticipated next move, one ply beyond GetSuggestedMove() - shown as a
    // second, visually distinct arrow, from the same m_PremoveCandidate PV[0..2] triple (see
    // OnEngineInfo):
    //   - Tracked player's own turn: what we expect the opponent to play back after our
    //     currently-suggested move - anchored against GetSuggestedMove(), since our move hasn't
    //     been played yet.
    //   - Opponent's turn: our planned response if the opponent plays the predicted move -
    //     anchored against the tracker's actual last-played move, since our half already
    //     happened.
    // Unlike TryPremove(), never consumes/clears m_PremoveCandidate, and re-derives its own
    // freshness check rather than relying on TryPremove()'s invalidation (which only runs while
    // both premove and autoplay are on) - this getter stays correct for display regardless of
    // those toggles. Returns nullopt if there's no candidate yet or it's stale relative to
    // whichever anchor applies. UI-thread-only, like GetTrackedBoard().
    [[nodiscard]] std::optional<std::string> GetLookaheadMove() const;

    // Other candidate first moves beyond GetSuggestedMove()'s primary line, each shown as its
    // own on-board arrow. Populated from UCI "multipv 2", "multipv 3", ... info lines (see
    // OnEngineInfo), which only arrive once the engine is asked to compute more than one line
    // (see kMultiPvLines / ControlsPanel::RestartEngine()). Ordered by ascending multipv index,
    // not necessarily by current live score. Empty if MultiPV is effectively off, no alternates
    // have arrived yet, or mid-request (cleared at the start of every RequestEngineMove()). Safe
    // to call from the UI thread.
    [[nodiscard]] std::vector<std::string> GetAlternateMoves() const;

    // Stockfish's own supported UCI_Elo range - shared with ControlsPanel so its Elo slider and
    // SetEloTarget's preset curve don't drift out of sync.
    static constexpr int kMinElo = 1320;
    static constexpr int kMaxElo = 3190;

    // Number of candidate lines the live engine computes (UCI "MultiPV") - 1 is the primary
    // line (GetSuggestedMove()), the rest feed GetAlternateMoves(). Set by
    // ControlsPanel::RestartEngine() every time the live engine (re)starts, since a freshly
    // spawned process defaults to MultiPV 1. Applies only to the live engine, not the sandbox's
    // dedicated one, which has no use for alternate lines.
    static constexpr int kMultiPvLines = 3;

    // Applies kMultiPvLines to controller's "MultiPV" option, if > 1 (no-op otherwise) - shared
    // by ControlsPanel::RestartEngine() (the live engine) and App::Run() (the sandbox's
    // dedicated engine, which has no "restart" button of its own) so both stay in sync on when
    // this gets (re)applied.
    static void ConfigureMultiPv(EngineController& controller);

    // Derives a movetime + depth preset from elo and applies it to future RequestEngineMove()
    // calls - a weaker target Elo thinks less deeply and less long. Independent of, and meant
    // to be paired with, the engine's own UCI_Elo strength limiting (set separately via
    // EngineController::SetOption). nullopt removes the cap: a generous fixed movetime, no depth
    // cap. Ignored while blitz mode is on. Call from the UI thread only.
    void SetEloTarget(std::optional<int> elo);

    // When enabled, overrides SetEloTarget's preset with a short, fixed movetime (see
    // kBlitzMoveTimeMs in the .cpp) so autoplay doesn't fall behind the clock in fast bot games.
    // Call from the UI thread only.
    void SetBlitzMode(bool enabled);
    [[nodiscard]] bool IsBlitzMode() const;

    // Experimental. When enabled (and autoplay is on), Poll() reacts to the opponent's move
    // without waiting out the configured Elo/Blitz search time, in two tiers:
    //   1. Instant: the opponent's move exactly matches our last search's predicted PV -
    //      immediately play the PV's next move, since the engine already evaluated it as best
    //      in exactly this resulting position.
    //   2. Quick-verify: the prediction missed or wasn't available - run a short, capped-time
    //      search of the actual position instead of the full-length one (still a real search,
    //      just trading some strength for matching a fast bot's pace). See
    //      kPremoveVerifyMoveTimeMs in the .cpp.
    // With this disabled, every move waits out the full configured search. Call from the UI
    // thread only.
    void SetPremoveEnabled(bool enabled);
    [[nodiscard]] bool IsPremoveEnabled() const;

    // Reads and parses path as a Polyglot opening book (see PolyglotBook), replacing whatever
    // was previously loaded. Returns false (book left empty) on any I/O or format failure - logs
    // the reason. Call from the UI thread only.
    bool LoadOpeningBook(const std::filesystem::path& path);
    [[nodiscard]] bool HasOpeningBookLoaded() const;

    // When enabled (and a book is loaded), RequestEngineMove() plays a book move for the
    // tracked player's own turn instead of searching, whenever the current position has one.
    // Falls back to normal engine search once the position leaves the book. Call from the UI
    // thread only.
    void SetOpeningBookEnabled(bool enabled);
    [[nodiscard]] bool IsOpeningBookEnabled() const;

    // How a book move is picked when the current position has more than one entry - see
    // PolyglotBook::SelectionMode. Call from the UI thread only.
    void SetBookSelectionMode(PolyglotBook::SelectionMode mode);

    // Average per-move accuracy for the tracked player so far this game, 0-100 - nullopt until
    // at least one move has been scored. For each move, this compares the engine's evaluation of
    // the position right before it against its evaluation right after (whatever was actually
    // played) - the drop ("centipawn loss") is converted to a 0-100 score via the same
    // exponential-decay curve chess.com's own accuracy metric uses, then averaged across the
    // game. A move only gets scored if both a "before" and an "after" search ran and completed -
    // a Blitz/premove-skipped position just isn't counted. Resets on ConnectToSite() and on
    // Poll() detecting a fresh game. Safe to call from the UI thread.
    [[nodiscard]] std::optional<float> GetAccuracyPercent() const;

private:
    // Which color the tracked player is on, inferred from board orientation - see
    // m_BlackAtBottom. Safe to call from either thread (m_BlackAtBottom is atomic).
    [[nodiscard]] PieceColor MyColor() const;

    // quickVerify caps this search to a short, fixed time (see kPremoveVerifyMoveTimeMs) - used
    // when premoving is armed but the opponent didn't play the predicted move, so autoplay still
    // responds fast instead of falling back to the full configured search length. See
    // SetPremoveEnabled's comment.
    void RequestEngineMove(bool quickVerify = false);
    void PlayMoveOnBoard(std::string_view uciMove);

    // Queues uciMove for Tick() to play, with a delay freshly rolled from m_MinMoveDelayMs/
    // m_MaxMoveDelayMs (see SetMoveDelay) - shared by OnEngineBestMove (an engine result for our
    // own turn) and RequestEngineMove (a book hit for our own turn). No-op if autoplay is off.
    // Callable from either thread, like the mutex it uses.
    void QueueAutoplayMove(const std::string& uciMove);

    // Checks lastAppliedMove against any pending premove candidate; plays the response and
    // returns true on a hit. Returns false if premoves are off, it's not the tracked player's
    // turn, or there's no matching candidate - callers should fall back to RequestEngineMove()
    // in that case.
    bool TryPremove(const std::string& lastAppliedMove);

    // True when every RequestEngineMove() call site should pass quickVerify=true - i.e.
    // premoving is armed and could hit, so a request that does go through the normal search
    // path should still stay fast.
    bool ShouldQuickVerify() const;

    // Clears all accuracy-tracking state - called on ConnectToSite() and on Poll() detecting a
    // fresh game, so accuracy is scoped to the current game.
    void ResetAccuracy();

    EngineController* m_Controller = nullptr;
    BrowserLauncher m_Launcher;
    CdpClient m_CdpClient;
    ChessSite m_Site = ChessSite::ChessDotCom;
    ChessRules m_Rules;
    GameTracker m_Tracker;
    bool m_Connected = false;
    bool m_Desynced = false;

    // See GetPositionGeneration()'s comment. Only bumped from Poll()/ConnectToSite(), both
    // UI-thread-only - but OnEngineInfo (engine reader thread) also reads it to stamp premove
    // candidates (see m_Premove.Update()), so it's atomic unlike the plain members around it.
    std::atomic<std::uint64_t> m_PositionGeneration{0};

    // Set once RequestEngineMove() has fired since the last ConnectToSite() - guards Poll()'s
    // NoChange branch so it seeds exactly one engine request for the starting position (0 moves
    // on the page, so the ordinary "newMoves non-empty" trigger never fires) without
    // re-requesting on every later no-change tick.
    bool m_InitialMoveRequested = false;

    // Written by Poll() (UI thread) from each extraction's orientation read, read by
    // OnEngineBestMove (engine reader thread) to infer which color the tracked player is on -
    // chess sites always show the logged-in player's own pieces at the bottom.
    std::atomic<bool> m_BlackAtBottom{false};
    std::atomic<bool> m_AutoplayEnabled{false};

    // Side to move when RequestEngineMove() (UI thread) issued the in-flight search - compared
    // against m_BlackAtBottom in OnEngineBestMove (reader thread) so a best-move result computed
    // for the opponent's turn never gets auto-played.
    std::atomic<PieceColor> m_RequestedForSide{PieceColor::White};

    std::mutex m_AutoMoveMutex;
    std::optional<std::string> m_PendingAutoMove;  // guarded by m_AutoMoveMutex

    // When m_PendingAutoMove is set, the time at which Tick() is allowed to actually play it -
    // computed once by OnEngineBestMove when the move is queued, rather than re-rolled every
    // frame. Guarded by m_AutoMoveMutex, like m_PendingAutoMove itself.
    std::chrono::steady_clock::time_point m_AutoMoveReadyTime;

    // Delay range applied before Tick() plays a queued autoplay move - see SetMoveDelay(). Set
    // from the UI thread but read from OnEngineBestMove (reader thread), hence atomic.
    std::atomic<int> m_MinMoveDelayMs{0};
    std::atomic<int> m_MaxMoveDelayMs{0};

    // Written by OnEngineBestMove (reader thread) and RequestEngineMove (UI thread, cleared
    // before every new search), read by GetSuggestedMove (UI thread). Separate from
    // m_PendingAutoMove: that one is consumed once by Tick(); this one persists as a stable
    // "here's the suggestion" value until superseded, regardless of whether autoplay is on.
    mutable std::mutex m_SuggestedMoveMutex;
    std::optional<std::string> m_SuggestedMove;  // guarded by m_SuggestedMoveMutex

    // See MultiPvCollector's own comment - written by OnEngineInfo (reader thread), cleared by
    // RequestEngineMove (UI thread) before every new search, read by GetAlternateMoves (UI
    // thread). Internally thread-safe, same shared type SandboxSession uses.
    MultiPvCollector m_AlternateMoves;

    // True when the in-flight search is purely cosmetic, for a turn the opening book already
    // decided (see RequestEngineMove) - its bestmove result must never overwrite m_SuggestedMove
    // or get auto-played, only feed OnEngineInfo's usual side effects. Written by
    // RequestEngineMove (UI thread) before every search, read by OnEngineBestMove (reader
    // thread) - safe because EngineController already discards a stale/superseded search's
    // bestmove before this class sees it, so this always reflects the request it's actually the
    // result of.
    std::atomic<bool> m_CosmeticSearch{false};

    // UI-thread-only (like RequestEngineMove() itself), same as m_Tracker/m_Rules - no
    // cross-thread access, so plain members rather than atomics.
    int m_MoveTimeMs = 1500;
    std::optional<int> m_SearchDepth;
    bool m_BlitzMode = false;

    std::atomic<bool> m_PremoveEnabled{false};

    // UI-thread-only, like m_BlitzMode (only set from ControlsPanel, only read from
    // RequestEngineMove).
    PolyglotBook m_OpeningBook;
    bool m_OpeningBookEnabled = false;
    PolyglotBook::SelectionMode m_BookSelectionMode = PolyglotBook::SelectionMode::WeightedRandom;

    // See PremoveTracker's own comment - written by OnEngineInfo (reader thread), consumed by
    // TryPremove (UI thread, via Poll()). Internally thread-safe.
    PremoveTracker m_Premove;

    // See AccuracyTracker's own comment - written by OnEngineInfo/OnEngineBestMove (reader
    // thread), read by GetAccuracyPercent (UI thread) and reset by ResetAccuracy (UI thread).
    // Internally thread-safe.
    AccuracyTracker m_Accuracy;
};
