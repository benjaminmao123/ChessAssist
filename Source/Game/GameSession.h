#pragma once

#include "GameTracker.h"

#include "Browser/BrowserLauncher.h"
#include "Browser/CdpClient.h"
#include "Browser/ChessSiteAdapter.h"
#include "Chess/ChessRules.h"
#include "Engine/EngineTypes.h"

#include <atomic>
#include <filesystem>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

class EngineController;

// Orchestrates a live game: launches and owns a dedicated Chrome instance (BrowserLauncher),
// polls the chess site's move-list DOM through it (CdpClient + ChessSiteAdapter), converts
// newly-seen SAN moves to UCI (ChessRules), and feeds them into the unchanged
// GameTracker/EngineController pipeline.
class GameSession
{
public:
    explicit GameSession(EngineController& controller);

    // One-time per app session: launches (or no-ops if already running) the app-managed
    // Chrome instance with CDP remote debugging enabled, navigated directly to site's
    // homepage rather than a blank tab.
    [[nodiscard]] std::expected<void, BrowserError> LaunchBrowser(const std::filesystem::path& profileDir, ChessSite site);
    [[nodiscard]] bool IsBrowserRunning() const;

    // Finds site's already-open tab in the app-managed Chrome, connects, and resets
    // tracking - the next Poll() baselines from whatever moves already exist in the site's
    // move list (0 for a fresh game, N for joining mid-game, handled automatically by Poll's
    // diff logic - no special-casing needed here).
    [[nodiscard]] bool ConnectToSite(ChessSite site);
    [[nodiscard]] bool IsConnected() const;

    // Ends the current session - closes the CDP connection and clears tracked state. Safe to
    // call even if not connected.
    void Disconnect();

    [[nodiscard]] const GameTracker& GetTracker() const;
    [[nodiscard]] const BoardState& GetTrackedBoard() const;

    // Square index of the tracked board's checked king, if either side is currently in check,
    // else nullopt - see ChessRules::CheckedKingSquare(). UI-thread-only, same as
    // GetTrackedBoard() (m_Rules is touched only from Poll()/Tick(), both UI-thread-only).
    // Display code (BoardStatePanel) uses this to highlight the checked king's square.
    [[nodiscard]] std::optional<int> GetCheckedKingSquare() const;

    // Re-runs the site's extraction script and applies any moves new since the last call.
    // Returns them, in order, as UCI. Requests a fresh engine move once at the end if
    // anything was applied - never more than once per call, regardless of how many moves
    // were newly discovered.
    [[nodiscard]] std::vector<std::string> Poll();

    // True once Poll() found the site's move list shrink in a way that isn't a clean
    // new-game reset, or a SAN move failed to parse - tracking can't be trusted until
    // ConnectToSite() is called again.
    [[nodiscard]] bool HasDesynced() const;

    // When enabled, an engine best-move result for the tracked player's own turn (inferred
    // from board orientation - see the class comment on m_BlackAtBottom) is automatically
    // played on the board. Turning it on re-requests a move for whatever position is
    // currently on the board (if connected), so autoplay acts immediately rather than only
    // from the next detected move onward. Call from the UI thread only - may call
    // RequestEngineMove(), which (like Poll()) touches main-thread-only state.
    void SetAutoplayEnabled(bool enabled);
    [[nodiscard]] bool IsAutoplayEnabled() const;

    // Routed here from EngineController::SetOnBestMove via main.cpp (which also forwards the
    // same result to EngineInfoPanel) - EngineController doesn't support multiple
    // subscribers, so main.cpp's callback fans out instead. Called from the engine's
    // background reader thread: must not touch m_CdpClient/m_Rules/m_Tracker directly (those
    // are main-thread-only, like Poll()) - only stashes the move for Tick() to play.
    void OnEngineBestMove(const BestMoveResult& result);

    // Routed here from EngineController::SetOnInfo via App (which also forwards the same
    // info to EngineInfoPanel) - feeds SetPremoveEnabled's premove detection (see its
    // comment); irrelevant to anything else here. Called from the engine's background reader
    // thread - same constraints as OnEngineBestMove.
    void OnEngineInfo(const SearchInfo& info);

    // Plays a pending autoplay move (queued by OnEngineBestMove), if any. Call once per frame
    // from the UI thread, alongside Poll().
    void Tick();

    // Side to move that the most recently requested engine search is analyzing - safe to
    // call from any thread (see m_RequestedForSide). UCI "score" is always relative to this
    // side, not to White, so display code needs it to flip the sign into White's-perspective
    // convention (positive = good for White) for the Black-to-move case.
    [[nodiscard]] PieceColor GetRequestedSide() const;

    // True if the tracked player's pieces render at the bottom of the site's board (i.e. the
    // player is Black) - see the member comment on m_BlackAtBottom. Safe to call from any
    // thread. Display code (BoardStatePanel) uses this to draw the tracked board in the same
    // orientation as the live game instead of always assuming White-at-bottom.
    [[nodiscard]] bool IsBlackAtBottom() const;

    // The engine's suggestion for the tracked player's own turn, as UCI (e.g. "e2e4") -
    // nullopt if none yet, it's currently the opponent's turn, or the position has moved on
    // since the last suggestion (RequestEngineMove() clears this before every new search, so
    // there's never a stale/wrong-position suggestion showing). Safe to call from the UI
    // thread. Display code (BoardStatePanel) draws this as an on-board arrow.
    [[nodiscard]] std::optional<std::string> GetSuggestedMove() const;

    // Stockfish's own supported UCI_Elo range - shared with ControlsPanel (which owns the Elo
    // slider and also forwards UCI_LimitStrength/UCI_Elo to EngineController directly) so the
    // two don't drift out of sync with each other or with SetEloTarget's own preset curve.
    static constexpr int kMinElo = 1320;
    static constexpr int kMaxElo = 3190;

    // Derives a movetime + depth preset from elo and applies it to future RequestEngineMove()
    // calls (an already-in-flight search is unaffected) - roughly, a weaker target Elo thinks
    // less deeply and less long, similar to how weaker bots play faster and shallower. This is
    // independent of, and meant to be paired with, the engine's own UCI_Elo strength limiting
    // (set separately via EngineController::SetOption - ControlsPanel owns both ends of the
    // single "Elo" UI control). nullopt removes the cap: a generous fixed movetime, no depth
    // cap - full-strength search. Ignored while blitz mode is on (see SetBlitzMode). Call from
    // the UI thread only.
    void SetEloTarget(std::optional<int> elo);

    // When enabled, overrides SetEloTarget's preset (or the no-cap default) with a short,
    // fixed movetime (see kBlitzMoveTimeMs in the .cpp) so autoplay doesn't fall behind the
    // clock in fast bot games. Call from the UI thread only.
    void SetBlitzMode(bool enabled);
    [[nodiscard]] bool IsBlitzMode() const;

    // Experimental. When enabled (and autoplay is also on), Poll() reacts to the opponent's
    // move without waiting out the configured Elo/Blitz search time, in two tiers:
    //   1. Instant: the opponent's move exactly matches what our last search's principal
    //      variation predicted they'd play - immediately play the PV's next move. Sound, not
    //      a guess: that move was already evaluated by the engine as best in exactly this
    //      resulting position, so playing it the instant the prediction is confirmed is
    //      equivalent to (just faster than) searching the position fresh.
    //   2. Quick-verify: the prediction missed (wrong guess, or none available yet - e.g. a
    //      shallow Elo/Blitz search whose PV didn't reach 3 plies deep). Rather than falling
    //      back to the full configured search time, run a short, capped-time search of the
    //      actual position instead of the full-length one - still a real search of the real
    //      position (never an unverified guess), just one that trades some strength for
    //      matching a fast bot's pace. See kPremoveVerifyMoveTimeMs in the .cpp.
    // With this disabled, every move waits out the full configured search regardless.
    // Call from the UI thread only.
    void SetPremoveEnabled(bool enabled);
    [[nodiscard]] bool IsPremoveEnabled() const;

    // Average per-move accuracy for the tracked player so far this game, 0-100 - nullopt
    // until at least one of their moves has been scored. For each of their moves, this
    // compares the engine's evaluation of the position right before the move (the best
    // achievable result) against its evaluation of the position right after the move actually
    // played landed (whether or not it was the suggested move) - the drop between the two
    // ("centipawn loss") is converted to a 0-100 score per move via the same
    // exponential-decay curve chess.com's own accuracy metric uses, then averaged across the
    // game. A move only gets scored if both a "before" and an "after" search actually ran and
    // completed - a Blitz/premove-skipped position just isn't counted, rather than guessed at.
    // Resets on ConnectToSite() and on Poll() detecting a fresh game. Safe to call from the UI
    // thread.
    [[nodiscard]] std::optional<float> GetAccuracyPercent() const;

private:
    // quickVerify caps this search to a short, fixed time (see kPremoveVerifyMoveTimeMs) -
    // used when premoving is armed but the opponent didn't play the predicted move (or no
    // prediction was available), so autoplay still responds fast instead of falling back to
    // the full configured Elo/Blitz search length. See SetPremoveEnabled's comment.
    void RequestEngineMove(bool quickVerify = false);
    void PlayMoveOnBoard(std::string_view uciMove);

    // Checks lastAppliedMove (the most recent move Poll() just applied) against any pending
    // premove candidate; plays the response and returns true on a hit. Returns false (nothing
    // played) if premoves are off, it's not now the tracked player's turn, or there's no
    // candidate / it doesn't match - callers should fall back to RequestEngineMove() in that
    // case, exactly as if this method didn't exist.
    bool TryPremove(const std::string& lastAppliedMove);

    // True when every RequestEngineMove() call site should pass quickVerify=true - i.e.
    // premoving is armed and could hit, so any request that does end up going through the
    // normal search path should still stay fast rather than falling back to the full
    // configured search length.
    bool ShouldQuickVerify() const;

    // Clears all accuracy-tracking state - called on ConnectToSite() and on Poll() detecting
    // a fresh game, so accuracy is scoped to the current game rather than accumulating
    // indefinitely across separate games in the same session.
    void ResetAccuracy();

    EngineController* m_Controller = nullptr;
    BrowserLauncher m_Launcher;
    CdpClient m_CdpClient;
    ChessSite m_Site = ChessSite::ChessDotCom;
    ChessRules m_Rules;
    GameTracker m_Tracker;
    bool m_Connected = false;
    bool m_Desynced = false;

    // Set once RequestEngineMove() has fired at least once since the last ConnectToSite() -
    // guards Poll()'s NoChange branch so it seeds exactly one engine request for the starting
    // position (0 moves on the page, so the ordinary "newMoves non-empty" trigger never fires)
    // without re-requesting on every subsequent no-change poll tick.
    bool m_InitialMoveRequested = false;

    // Written by Poll() (UI thread) from each extraction's best-effort orientation read, read
    // by OnEngineBestMove (engine reader thread) to infer which color the tracked player (and
    // so the autoplaying engine) is on - chess sites always show the logged-in player's own
    // pieces at the bottom, so bottom-orientation doubles as a "which side is ours" signal.
    std::atomic<bool> m_BlackAtBottom{false};
    std::atomic<bool> m_AutoplayEnabled{false};

    // Side to move at the moment RequestEngineMove() (UI thread) issued the in-flight search -
    // compared against m_BlackAtBottom in OnEngineBestMove (reader thread) so a best-move
    // result computed for the opponent's turn (e.g. purely informational display, or one that
    // arrives just as the opponent's own move lands) never gets auto-played.
    std::atomic<PieceColor> m_RequestedForSide{PieceColor::White};

    std::mutex m_AutoMoveMutex;
    std::optional<std::string> m_PendingAutoMove;  // guarded by m_AutoMoveMutex

    // Written by OnEngineBestMove (reader thread) and RequestEngineMove (UI thread, to clear
    // it before every new search - see GetSuggestedMove()'s comment), read by GetSuggestedMove
    // (UI thread, for display). Separate from m_PendingAutoMove: that one is consumed once by
    // Tick(); this one persists across frames as a stable "here's the suggestion" value until
    // superseded, regardless of whether autoplay is even on.
    mutable std::mutex m_SuggestedMoveMutex;
    std::optional<std::string> m_SuggestedMove;  // guarded by m_SuggestedMoveMutex

    // UI-thread-only (like RequestEngineMove() itself), same as m_Tracker/m_Rules - no
    // cross-thread access, so plain members rather than atomics.
    int m_MoveTimeMs = 1500;
    std::optional<int> m_SearchDepth;
    bool m_BlitzMode = false;

    std::atomic<bool> m_PremoveEnabled{false};

    // ExpectedOwnMove/PredictedOpponentMove/OurResponse are PV[0]/PV[1]/PV[2] from our own
    // last search (see OnEngineInfo) - "we expect to play this; if the opponent then plays
    // this, our best reply is this". Written by OnEngineInfo (reader thread), consumed and
    // cleared by TryPremove (UI thread, via Poll()) - both when it fires (matched) and when it
    // determines a stored candidate no longer applies (ExpectedOwnMove didn't match what we
    // actually just played, e.g. a human overrode the suggestion). Deliberately NOT cleared by
    // RequestEngineMove(): the informational request made for the opponent's turn immediately
    // after our own move is exactly the position this candidate is waiting out, so clearing it
    // there (as an earlier version of this code did) meant premove could never fire at all.
    struct PremoveCandidate
    {
        std::string ExpectedOwnMove;
        std::string PredictedOpponentMove;
        std::string OurResponse;
    };
    mutable std::mutex m_PremoveMutex;
    std::optional<PremoveCandidate> m_PremoveCandidate;  // guarded by m_PremoveMutex

    // All guarded by m_AccuracyMutex - written by OnEngineInfo/OnEngineBestMove (reader
    // thread), read by GetAccuracyPercent (UI thread) and reset by ResetAccuracy (UI thread).
    // m_PendingBeforeMoveEvalCp/m_LatestAfterMoveEvalCp are both in the tracked player's own
    // perspective (positive = good for them), continuously overwritten ("last update wins",
    // same pattern as m_SuggestedMove/m_PremoveCandidate) while their respective side's search
    // is in flight; OnEngineBestMove pairs them off once the opponent-turn search completes.
    mutable std::mutex m_AccuracyMutex;
    std::optional<float> m_PendingBeforeMoveEvalCp;
    std::optional<float> m_LatestAfterMoveEvalCp;
    double m_AccuracySumPercent = 0.0;
    int m_AccuracyMoveCount = 0;
};
