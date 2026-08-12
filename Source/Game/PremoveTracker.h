#pragma once

#include <mutex>
#include <optional>
#include <string>

// Tracks "if we play this, and they reply with this, our best next move is this" - a 3-ply PV
// snapshot (Candidate::ExpectedOwnMove/PredictedOpponentMove/OurResponse, PV[0]/PV[1]/PV[2])
// from the tracked player's own last search. Consumed by GameSession::TryPremove() to play the
// response instantly once the opponent's actual move confirms the prediction, and peeked
// (without consuming) by GameSession::GetLookaheadMove() to draw it as an on-board arrow.
// Extracted out of GameSession since it's a self-contained state machine independent of
// everything else that class does.
//
// Deliberately has no plain "clear" - a fresh search request for the opponent's own turn
// (issued right after our own move, exactly the position this candidate is waiting out) must
// NOT invalidate it, or premoves could never fire at all; only Take()/InvalidateIfMismatched()
// ever remove a stored candidate, both driven by TryPremove()'s own logic. Internally
// thread-safe: Update() is meant to be called from the engine's background reader thread (see
// GameSession::OnEngineInfo), everything else from the UI thread.
class PremoveTracker
{
public:
    struct Candidate
    {
        std::string ExpectedOwnMove;
        std::string PredictedOpponentMove;
        std::string OurResponse;
    };

    void Update(std::string expectedOwnMove, std::string predictedOpponentMove, std::string ourResponse);

    // Non-consuming read of whatever candidate is currently stored, for GetLookaheadMove()'s
    // own freshness check against whichever anchor applies - unlike Take()/
    // InvalidateIfMismatched(), never modifies state.
    [[nodiscard]] std::optional<Candidate> Peek() const;

    // Unconditionally takes (clears) whatever candidate is stored, if any - used once it's
    // confirmed to be the tracked player's turn again (the opponent's move already landed), so
    // whatever candidate was waiting either just got its chance to fire or is now moot either
    // way.
    [[nodiscard]] std::optional<Candidate> Take();

    // Discards the stored candidate only if it doesn't match expectedOwnMove - the "we just
    // moved, but it wasn't what the candidate assumed we'd play" invalidation. No-op if there's
    // no candidate, or it already matches.
    void InvalidateIfMismatched(const std::string& expectedOwnMove);

private:
    mutable std::mutex m_Mutex;
    std::optional<Candidate> m_Candidate;
};
