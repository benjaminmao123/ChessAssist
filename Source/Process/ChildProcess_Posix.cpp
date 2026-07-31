#include "ChildProcess.h"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

extern char** environ;

struct ChildProcess::Impl
{
    pid_t Pid = -1;
    int StdInWriteFd = -1;
    int StdOutReadFd = -1;
    bool Running = false;
    std::string ReadBuffer;

    ~Impl()
    {
        Cleanup();
    }

    void Cleanup()
    {
        if (Running)
        {
            kill(Pid, SIGTERM);

            int status = 0;
            bool exited = false;

            // Give the process a brief window to exit on its own (e.g. after the caller
            // already sent "quit") before forcefully killing it.
            for (int i = 0; i < 50; ++i)
            {
                if (waitpid(Pid, &status, WNOHANG) != 0)
                {
                    exited = true;
                    break;
                }

                usleep(10000);  // 10ms
            }

            if (!exited)
            {
                kill(Pid, SIGKILL);
                waitpid(Pid, &status, 0);
            }

            Running = false;
        }

        if (StdInWriteFd >= 0)
        {
            close(StdInWriteFd);
            StdInWriteFd = -1;
        }

        if (StdOutReadFd >= 0)
        {
            close(StdOutReadFd);
            StdOutReadFd = -1;
        }
    }
};

ChildProcess::ChildProcess()
    : m_Impl(std::make_unique<Impl>())
{
}

ChildProcess::~ChildProcess() = default;
ChildProcess::ChildProcess(ChildProcess&&) noexcept = default;
ChildProcess& ChildProcess::operator=(ChildProcess&&) noexcept = default;

std::expected<void, EngineError> ChildProcess::Start(const std::filesystem::path& executablePath, std::span<const std::string> arguments)
{
    int stdInPipe[2] = {-1, -1};
    int stdOutPipe[2] = {-1, -1};

    if (pipe(stdInPipe) != 0)
        return std::unexpected(EngineError{EngineErrorCode::ProcessLaunchFailed, std::string("Failed to create stdin pipe: ") + std::strerror(errno)});

    if (pipe(stdOutPipe) != 0)
    {
        close(stdInPipe[0]);
        close(stdInPipe[1]);
        return std::unexpected(EngineError{EngineErrorCode::ProcessLaunchFailed, std::string("Failed to create stdout pipe: ") + std::strerror(errno)});
    }

    posix_spawn_file_actions_t fileActions;
    posix_spawn_file_actions_init(&fileActions);
    posix_spawn_file_actions_adddup2(&fileActions, stdInPipe[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&fileActions, stdOutPipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&fileActions, stdOutPipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&fileActions, stdInPipe[0]);
    posix_spawn_file_actions_addclose(&fileActions, stdInPipe[1]);
    posix_spawn_file_actions_addclose(&fileActions, stdOutPipe[0]);
    posix_spawn_file_actions_addclose(&fileActions, stdOutPipe[1]);

    std::string executablePathStr = executablePath.string();

    std::vector<std::string> argumentStorage(arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.push_back(executablePathStr.data());
    for (std::string& argument : argumentStorage)
        argv.push_back(argument.data());
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawnResult = posix_spawn(&pid, executablePathStr.c_str(), &fileActions, nullptr, argv.data(), environ);

    posix_spawn_file_actions_destroy(&fileActions);

    // Parent must close the child-side ends regardless of outcome, or reads on the stdout
    // pipe will block forever even after the child exits.
    close(stdInPipe[0]);
    close(stdOutPipe[1]);

    if (spawnResult != 0)
    {
        close(stdInPipe[1]);
        close(stdOutPipe[0]);
        return std::unexpected(EngineError{EngineErrorCode::ProcessLaunchFailed, std::string("posix_spawn failed: ") + std::strerror(spawnResult)});
    }

    m_Impl->Pid = pid;
    m_Impl->StdInWriteFd = stdInPipe[1];
    m_Impl->StdOutReadFd = stdOutPipe[0];
    m_Impl->Running = true;

    return {};
}

bool ChildProcess::WriteLine(std::string_view line)
{
    if (!m_Impl->Running || m_Impl->StdInWriteFd < 0)
        return false;

    std::string buffer(line);
    buffer += '\n';

    std::size_t totalWritten = 0;
    while (totalWritten < buffer.size())
    {
        const ssize_t written = write(m_Impl->StdInWriteFd, buffer.data() + totalWritten, buffer.size() - totalWritten);
        if (written <= 0)
            return false;

        totalWritten += static_cast<std::size_t>(written);
    }

    return true;
}

std::optional<std::string> ChildProcess::ReadLine()
{
    if (m_Impl->StdOutReadFd < 0)
        return std::nullopt;

    for (;;)
    {
        const std::size_t newlinePos = m_Impl->ReadBuffer.find('\n');
        if (newlinePos != std::string::npos)
        {
            std::string line = m_Impl->ReadBuffer.substr(0, newlinePos);
            m_Impl->ReadBuffer.erase(0, newlinePos + 1);

            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            return line;
        }

        std::array<char, 4096> chunk{};
        const ssize_t bytesRead = read(m_Impl->StdOutReadFd, chunk.data(), chunk.size());

        if (bytesRead <= 0)
            return std::nullopt;

        m_Impl->ReadBuffer.append(chunk.data(), static_cast<std::size_t>(bytesRead));
    }
}

bool ChildProcess::IsRunning() const
{
    if (!m_Impl->Running)
        return false;

    int status = 0;
    if (waitpid(m_Impl->Pid, &status, WNOHANG) != 0)
    {
        m_Impl->Running = false;
        return false;
    }

    return true;
}

void ChildProcess::Terminate()
{
    m_Impl->Cleanup();
}

int ChildProcess::Wait()
{
    if (m_Impl->Pid < 0)
        return -1;

    int status = 0;
    waitpid(m_Impl->Pid, &status, 0);
    m_Impl->Running = false;

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return -1;
}
