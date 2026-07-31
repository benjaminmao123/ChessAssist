#include "EngineController.h"
#include "ExecutablePathUtil.h"
#include "UCIProtocol.h"

#include <chrono>

EngineController::EngineController()
    : m_Client(std::make_unique<UCIClient>())
{
}

EngineController::~EngineController()
{
    Shutdown();
}

std::expected<void, EngineError> EngineController::Start(std::optional<std::filesystem::path> enginePath)
{
    std::filesystem::path path = enginePath.value_or(ExecutablePathUtil::GetDefaultStockfishPath());

    if (!std::filesystem::exists(path))
        return std::unexpected(EngineError{EngineErrorCode::ExecutableNotFound, "Stockfish executable not found at " + path.string()});

    if (auto result = m_Client->Start(path); !result)
        return std::unexpected(result.error());

    if (auto result = m_Client->PerformHandshake(); !result)
        return std::unexpected(result.error());

    if (auto result = m_Client->WaitUntilReady(); !result)
        return std::unexpected(result.error());

    m_ShuttingDown = false;
    m_ReaderThread = std::thread(&EngineController::ReaderThreadLoop, this);

    return {};
}

void EngineController::Shutdown()
{
    if (m_ShuttingDown.exchange(true))
        return;

    if (m_Client && m_Client->IsRunning())
    {
        m_Client->SendQuit();

        // Give the engine a brief window to exit on its own - its stdout pipe closing is
        // what unblocks the reader thread's blocking read - before forcing it.
        for (int i = 0; i < 50 && m_Client->IsRunning(); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

        if (m_Client->IsRunning())
            m_Client->Terminate();
    }

    if (m_ReaderThread.joinable())
        m_ReaderThread.join();

    std::optional<std::promise<std::expected<BestMoveResult, EngineError>>> pending;
    {
        std::scoped_lock lock(m_PendingMutex);
        pending = std::move(m_PendingBestMove);
        m_PendingBestMove.reset();
    }

    if (pending)
        pending->set_value(std::unexpected(EngineError{EngineErrorCode::ProcessNotRunning, "Engine shut down while a search was in progress"}));
}

std::expected<BestMoveResult, EngineError> EngineController::FindBestMove(std::string_view fen, const SearchLimits& limits, std::span<const std::string> moves)
{
    return FindBestMoveAsync(fen, limits, moves).get();
}

std::future<std::expected<BestMoveResult, EngineError>> EngineController::FindBestMoveAsync(std::string_view fen, const SearchLimits& limits, std::span<const std::string> moves)
{
    std::promise<std::expected<BestMoveResult, EngineError>> promise;
    std::future<std::expected<BestMoveResult, EngineError>> future = promise.get_future();

    if (!m_Client || !m_Client->IsRunning())
    {
        promise.set_value(std::unexpected(EngineError{EngineErrorCode::ProcessNotRunning, "Engine is not running"}));
        return future;
    }

    const std::uint64_t requestId = ++m_RequestGeneration;

    // Serializes concurrent callers: a second call blocks here until the in-flight
    // search's bestmove line arrives and HandleBestMoveLine clears the flag below. If a
    // search is already running, stop it immediately rather than waiting out its full
    // movetime - this request already supersedes it (see m_RequestGeneration), so there's
    // no reason to block the caller (GameSession calls this synchronously from the poll
    // loop) for however long was left of the old search's time budget.
    std::unique_lock<std::mutex> searchLock(m_SearchMutex);
    if (m_SearchInProgress)
    {
        searchLock.unlock();
        m_Client->SendStop();
        searchLock.lock();
    }
    m_SearchCv.wait(searchLock, [this] { return !m_SearchInProgress; });
    m_SearchInProgress = true;
    searchLock.unlock();

    {
        std::scoped_lock lock(m_PendingMutex);
        m_PendingBestMove = std::move(promise);
        m_PendingRequestId = requestId;
    }

    m_Client->SendPosition(fen, moves);
    m_Client->SendGo(limits);

    return future;
}

void EngineController::StopSearch()
{
    if (m_Client)
        m_Client->SendStop();
}

void EngineController::SetOnInfo(InfoCallback callback)
{
    std::scoped_lock lock(m_CallbackMutex);
    m_OnInfo = std::move(callback);
}

void EngineController::SetOnBestMove(BestMoveCallback callback)
{
    std::scoped_lock lock(m_CallbackMutex);
    m_OnBestMove = std::move(callback);
}

bool EngineController::IsRunning() const
{
    return m_Client && m_Client->IsRunning();
}

void EngineController::ReaderThreadLoop()
{
    while (auto line = m_Client->ReadLine())
    {
        if (line->starts_with("info"))
            HandleInfoLine(*line);
        else if (line->starts_with("bestmove"))
            HandleBestMoveLine(*line);
    }
}

void EngineController::HandleInfoLine(std::string_view line)
{
    auto info = UCIProtocol::ParseInfoLine(line);
    if (!info)
        return;

    InfoCallback callback;
    {
        std::scoped_lock lock(m_CallbackMutex);
        callback = m_OnInfo;
    }

    if (callback)
        callback(*info);
}

void EngineController::HandleBestMoveLine(std::string_view line)
{
    auto result = UCIProtocol::ParseBestMoveLine(line);

    std::optional<std::promise<std::expected<BestMoveResult, EngineError>>> pending;
    std::uint64_t pendingRequestId = 0;
    {
        std::scoped_lock lock(m_PendingMutex);
        pending = std::move(m_PendingBestMove);
        pendingRequestId = m_PendingRequestId;
        m_PendingBestMove.reset();
    }

    if (pending)
    {
        if (result)
            pending->set_value(*result);
        else
            pending->set_value(std::unexpected(EngineError{EngineErrorCode::HandshakeFailed, "Failed to parse bestmove line from engine"}));
    }

    {
        std::lock_guard<std::mutex> lock(m_SearchMutex);
        m_SearchInProgress = false;
    }
    m_SearchCv.notify_one();

    // A newer request may have already superseded this one (see m_RequestGeneration's
    // comment in the header) - if so, this result is for a position that's no longer
    // current, so it must not reach the UI even though it's a perfectly valid engine result.
    const bool isStale = pendingRequestId != m_RequestGeneration.load();

    if (result && !isStale)
    {
        BestMoveCallback callback;
        {
            std::scoped_lock lock(m_CallbackMutex);
            callback = m_OnBestMove;
        }

        if (callback)
            callback(*result);
    }
}
