#pragma once

#include "../Engine/EngineTypes.h"

#include <expected>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class ChildProcess
{
public:
    ChildProcess();
    ~ChildProcess();
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ChildProcess(ChildProcess&&) noexcept;
    ChildProcess& operator=(ChildProcess&&) noexcept;

    [[nodiscard]] std::expected<void, EngineError> Start(const std::filesystem::path& executablePath, std::span<const std::string> arguments = {});

    [[nodiscard]] bool WriteLine(std::string_view line);  // appends '\n'; false on failure
    [[nodiscard]] std::optional<std::string> ReadLine();  // blocking; nullopt on EOF

    [[nodiscard]] bool IsRunning() const;
    void Terminate();  // graceful-then-forceful; idempotent
    int Wait();        // blocks for exit, reaps process

private:
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};
