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

    // Sends "setoption name <name> value <value>" - e.g. UCI_LimitStrength/UCI_Elo to cap
    // playing strength. Takes effect for future searches, not one already in flight. A fresh
    // engine process starts with every option at its default, so callers that restart the
    // engine (see ControlsPanel::RestartEngine) are responsible for re-sending anything that
    // needs to persist across a restart - this class has no memory of previously-set options.
    // Returns false without sending anything if the engine isn't running.
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

    // Guards against displaying a stale bestmove: GameSession fires a new search reactively
    // every time a move is detected, without waiting for the previous search (for the
    // position before that move) to finish - if moves come in faster than the search's
    // movetime, an old search's result can arrive after a newer one has already been
    // requested. m_RequestGeneration is bumped on every FindBestMoveAsync call (before it
    // may block waiting for an in-flight search), and the generation active when a result
    // was requested is compared against it when the result comes back: if a newer request
    // exists, this one is stale and its OnBestMove callback is suppressed (its promise is
    // still fulfilled, so blocking FindBestMove() callers aren't left hanging).
    std::atomic<std::uint64_t> m_RequestGeneration{0};

    // Request ID of the search whose "position"/"go" was most recently actually sent to the
    // engine (set in FindBestMoveAsync right alongside SendPosition/SendGo). Unlike bestmove
    // lines, UCI "info" lines carry no id linking them back to a request, so HandleInfoLine
    // can't compare against a per-line request ID - it instead compares this against
    // m_RequestGeneration: if a newer request already exists (e.g. one that's still waiting
    // for this search's own stop-triggered bestmove before it can send its own position/go),
    // any info line arriving in the meantime belongs to a search that's already superseded
    // and must be discarded rather than shown - a search stopped microseconds after starting
    // can otherwise surface a placeholder line like "info depth 0 score mate 0" that then
    // sits in the UI, indistinguishable from a real result, until fresher info arrives.
    std::atomic<std::uint64_t> m_ActiveSearchRequestId{0};

    std::mutex m_PendingMutex;
    std::optional<std::promise<std::expected<BestMoveResult, EngineError>>> m_PendingBestMove;
    std::uint64_t m_PendingRequestId = 0;

    std::mutex m_CallbackMutex;
    InfoCallback m_OnInfo;
    BestMoveCallback m_OnBestMove;
};
