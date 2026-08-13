#include "UCIClient.h"
#include "UCIProtocol.h"

UCIClient::UCIClient()
    : m_Process(std::make_unique<ChildProcess>())
{
}

UCIClient::~UCIClient() = default;

std::expected<void, EngineError> UCIClient::Start(const std::filesystem::path& enginePath)
{
    return m_Process->Start(enginePath);
}

std::expected<void, EngineError> UCIClient::PerformHandshake()
{
    if (!m_Process->WriteLine("uci"))
        return std::unexpected(EngineError{EngineErrorCode::WriteFailed, "Failed to send 'uci' command"});

    while (auto line = m_Process->ReadLine()) {
        if (*line == "uciok")
            return {};

        // "id name/author ..." and "option ..." lines are skipped - no engine option
        // configuration is exposed by this layer yet.
    }

    return std::unexpected(EngineError{EngineErrorCode::HandshakeFailed, "Engine process ended before sending 'uciok'"});
}

std::expected<void, EngineError> UCIClient::WaitUntilReady()
{
    if (!m_Process->WriteLine("isready"))
        return std::unexpected(EngineError{EngineErrorCode::WriteFailed, "Failed to send 'isready' command"});

    while (auto line = m_Process->ReadLine()) {
        if (*line == "readyok")
            return {};
    }

    return std::unexpected(EngineError{EngineErrorCode::HandshakeFailed, "Engine process ended before sending 'readyok'"});
}

void UCIClient::SendNewGame()
{
    (void)m_Process->WriteLine("ucinewgame");
}

void UCIClient::SendPosition(std::string_view fen, std::span<const std::string> moves)
{
    (void)m_Process->WriteLine(UCIProtocol::BuildPositionCommand(fen, moves));
}

void UCIClient::SendGo(const SearchLimits& limits)
{
    (void)m_Process->WriteLine(UCIProtocol::BuildGoCommand(limits));
}

void UCIClient::SendStop()
{
    (void)m_Process->WriteLine("stop");
}

void UCIClient::SendQuit()
{
    (void)m_Process->WriteLine("quit");
}

bool UCIClient::SendSetOption(std::string_view name, std::string_view value)
{
    return m_Process->WriteLine("setoption name " + std::string(name) + " value " + std::string(value));
}

std::optional<std::string> UCIClient::ReadLine()
{
    return m_Process->ReadLine();
}

bool UCIClient::IsRunning() const
{
    return m_Process->IsRunning();
}

void UCIClient::Terminate()
{
    m_Process->Terminate();
}
