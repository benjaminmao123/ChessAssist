#pragma once

#include "../Engine/EngineTypes.h"

#include <mutex>
#include <optional>

// Bridges EngineController's background reader thread to the UI thread. ImGui is not
// thread-safe, so UpdateInfo/UpdateBestMove (called from the engine's reader thread) only
// stash a copy behind a mutex - Draw (called once per frame from the UI thread) reads it.
class EngineInfoPanel
{
public:
    void UpdateInfo(const SearchInfo& info);
    void UpdateBestMove(const BestMoveResult& result);

    // Not thread-safe: call once per frame from the UI thread only.
    void Draw();

private:
    std::mutex m_Mutex;
    std::optional<SearchInfo> m_LatestInfo;
    std::optional<BestMoveResult> m_LatestBestMove;
};
