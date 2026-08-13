#pragma once

#include "EngineTypes.h"
#include "UCIClient.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>

class EngineController
{
public:
    using InfoCallback = std::function<void(const SearchInfo&)>;
    using BestMoveCallback = std::function<void(const BestMoveResult&)>;

    EngineController();
    ~EngineController();
    EngineController(const EngineController&) = delete;
    EngineController& operator=(const EngineController&) = delete;

    [[nodiscard]] std::expected<void, EngineError> Start(std::optional<std::filesystem::path> enginePath = std::nullopt);
    void Shutdown();

    [[nodiscard]] std::expected<BestMoveResult, EngineError> FindBestMove(std::string_view fen, const SearchLimits& limits, std::span<const std::string> moves = {});
    [[nodiscard]] std::future<std::expected<BestMoveResult, EngineError>> FindBestMoveAsync(std::string_view fen, const SearchLimits& limits, std::span<const std::string> moves = {});
    void StopSearch();

    void SetOnInfo(InfoCallback callback);
    void SetOnBestMove(BestMoveCallback callback);
    [[nodiscard]] bool IsRunning() const;

    // Sends "setoption name <name> value <value>" (e.g. UCI_LimitStrength/UCI_Elo). Applies to
    // future searches only; a restarted engine resets to defaults, so callers (see
    // ControlsPanel::RestartEngine) must re-send anything that needs to persist.
    bool SetOption(std::string_view name, std::string_view value);

private:
    void ReaderThreadLoop();
    void HandleInfoLine(std::string_view line);
    void HandleBestMoveLine(std::string_view line);

    std::unique_ptr<UCIClient> m_Client;
    std::thread m_ReaderThread;
    std::atomic<bool> m_ShuttingDown{false};

    std::mutex m_SearchMutex;
    std::condition_variable m_SearchCv;
    bool m_SearchInProgress = false;

    // Guards against a stale bestmove: GameSession can fire a new search before the previous
    // one's result arrives, so this is bumped per FindBestMoveAsync call and compared against
    // the requesting generation when a result comes back - a mismatch means the result is
    // stale and its OnBestMove callback is suppressed (the promise is still fulfilled so
    // blocking callers aren't left hanging).
    std::atomic<std::uint64_t> m_RequestGeneration{0};

    // Request ID of the search most recently sent to the engine. UCI "info" lines carry no
    // request id, so HandleInfoLine compares this against m_RequestGeneration instead - if a
    // newer request already exists, the info line belongs to a superseded search and is
    // discarded (otherwise a search stopped right after starting can leave a stale placeholder
    // line like "info depth 0 score mate 0" sitting in the UI).
    std::atomic<std::uint64_t> m_ActiveSearchRequestId{0};

    std::mutex m_PendingMutex;
    std::optional<std::promise<std::expected<BestMoveResult, EngineError>>> m_PendingBestMove;
    std::uint64_t m_PendingRequestId = 0;

    std::mutex m_CallbackMutex;
    InfoCallback m_OnInfo;
    BestMoveCallback m_OnBestMove;
};
