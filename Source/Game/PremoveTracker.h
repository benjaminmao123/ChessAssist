#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

// Tracks "if we play this, and they reply with this, our best next move is this" - a 3-ply PV
// snapshot (Candidate::ExpectedOwnMove/PredictedOpponentMove/OurResponse, PV[0]/PV[1]/PV[2])
// from the tracked player's own last search. Consumed by GameSession::TryPremove() to play the
// response instantly once the opponent's actual move confirms the prediction, and peeked
// (without consuming) by GameSession::GetLookaheadMove() to draw it as an on-board arrow.
//
// Deliberately has no plain "clear": a fresh search request for the opponent's own turn (issued
// right after our own move, exactly the position this candidate is waiting out) must NOT
// invalidate it, or premoves could never fire. Only Take()/InvalidateIfMismatched() ever remove
// a stored candidate. Internally thread-safe: Update() is called from the engine's background
// reader thread (see GameSession::OnEngineInfo), everything else from the UI thread.
class PremoveTracker
{
public:
    struct Candidate
    {
        std::string ExpectedOwnMove;
        std::string PredictedOpponentMove;
        std::string OurResponse;

        // GameSession::GetPositionGeneration() at the moment this candidate was computed - lets
        // GetLookaheadMove() detect staleness even when ExpectedOwnMove's *string* happens to
        // coincide with the current anchor again later in the game (e.g. the same retreat square
        // suggested twice), which a plain string comparison can't catch since this candidate is
        // never cleared on its own (see the class comment).
        std::uint64_t Generation = 0;
    };

    void Update(std::string expectedOwnMove, std::string predictedOpponentMove, std::string ourResponse, std::uint64_t generation);

    // Non-consuming read of whatever candidate is stored, for GetLookaheadMove()'s own
    // freshness check - unlike Take()/InvalidateIfMismatched(), never modifies state.
    [[nodiscard]] std::optional<Candidate> Peek() const;

    // Unconditionally takes (clears) the stored candidate, if any - used once it's confirmed to
    // be the tracked player's turn again, so the waiting candidate either just got its chance to
    // fire or is now moot.
    [[nodiscard]] std::optional<Candidate> Take();

    // Discards the stored candidate only if it doesn't match expectedOwnMove - the "we just
    // moved, but it wasn't what the candidate assumed we'd play" invalidation.
    void InvalidateIfMismatched(const std::string& expectedOwnMove);

private:
    mutable std::mutex m_Mutex;
    std::optional<Candidate> m_Candidate;
};
