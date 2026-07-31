#pragma once

#include "EngineTypes.h"
#include "../Process/ChildProcess.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class UCIClient
{
public:
    UCIClient();
    ~UCIClient();
    UCIClient(const UCIClient&) = delete;
    UCIClient& operator=(const UCIClient&) = delete;

    [[nodiscard]] std::expected<void, EngineError> Start(const std::filesystem::path& enginePath);
    [[nodiscard]] std::expected<void, EngineError> PerformHandshake();
    [[nodiscard]] std::expected<void, EngineError> WaitUntilReady();

    void SendNewGame();
    void SendPosition(std::string_view fen, std::span<const std::string> moves = {});
    void SendGo(const SearchLimits& limits);
    void SendStop();
    void SendQuit();
    bool SendSetOption(std::string_view name, std::string_view value);

    [[nodiscard]] std::optional<std::string> ReadLine();
    [[nodiscard]] bool IsRunning() const;
    void Terminate();

private:
    std::unique_ptr<ChildProcess> m_Process;
};
