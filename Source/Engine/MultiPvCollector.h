#pragma once

#include "EngineTypes.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

// Collects "other candidate first moves" (UCI multipv >= 2 info lines) for display as secondary
// on-board arrows alongside a search's primary suggestion - identical logic previously
// duplicated between GameSession and SandboxSession, which both feed it the same way from their
// own OnEngineInfo. Keyed internally by multipv index (2, 3, ...) rather than a plain vector so
// an out-of-order or missing line for one index can't shift/mislabel another's slot -
// GetMoves()'s "ordered by multipv index" guarantee is then free from std::map's own key
// ordering. Internally thread-safe: OnInfo is meant to be called from the engine's background
// reader thread, GetMoves()/Clear() from the UI thread.
class MultiPvCollector
{
public:
    // Records info's first PV move under its multipv index - a no-op if info.MultiPvIndex < 2
    // (the primary line isn't this collector's concern) or its PV is empty.
    void OnInfo(const SearchInfo& info)
    {
        if (info.MultiPvIndex < 2 || info.Pv.empty())
            return;

        std::scoped_lock lock(m_Mutex);
        m_Moves[info.MultiPvIndex] = info.Pv.front();
    }

    // Called before every new search so a stale alternate from the previous position doesn't
    // linger until the new search's own multipv lines start arriving.
    void Clear()
    {
        std::scoped_lock lock(m_Mutex);
        m_Moves.clear();
    }

    [[nodiscard]] std::vector<std::string> GetMoves() const
    {
        std::scoped_lock lock(m_Mutex);

        std::vector<std::string> moves;
        moves.reserve(m_Moves.size());
        for (const auto& [multiPvIndex, move] : m_Moves)
            moves.push_back(move);  // std::map iterates by key, so this is already index-ordered

        return moves;
    }

private:
    mutable std::mutex m_Mutex;
    std::map<int, std::string> m_Moves;  // guarded by m_Mutex
};
