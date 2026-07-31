#pragma once

#include "EngineTypes.h"
#include "UCIClient.h"

#include <atomic>
#include <condition_variable>
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

    std::mutex m_PendingMutex;
    std::optional<std::promise<std::expected<BestMoveResult, EngineError>>> m_PendingBestMove;

    std::mutex m_CallbackMutex;
    InfoCallback m_OnInfo;
    BestMoveCallback m_OnBestMove;
};
