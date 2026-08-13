#pragma once

#include "EngineTypes.h"

#include <map>
#include <mutex>
#include <string>
#include <vector>

// Collects "other candidate first moves" (UCI multipv >= 2 info lines) for display as secondary
// on-board arrows alongside a search's primary suggestion. Keyed by multipv index rather than a
// plain vector so an out-of-order or missing line can't shift/mislabel another slot. Thread-safe:
// OnInfo runs on the engine's reader thread, GetMoves()/Clear() on the UI thread.
class MultiPvCollector
{
public:
    // No-op if info.MultiPvIndex < 2 (the primary line) or its PV is empty.
    void OnInfo(const SearchInfo& info)
    {
        if (info.MultiPvIndex < 2 || info.Pv.empty())
            return;

        std::scoped_lock lock(m_Mutex);
        m_Moves[info.MultiPvIndex] = info.Pv.front();
    }

    // Call before every new search so a stale alternate from the previous position doesn't
    // linger until the new search's own multipv lines arrive.
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
