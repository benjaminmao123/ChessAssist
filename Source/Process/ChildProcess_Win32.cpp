#include "ChildProcess.h"

#define NOMINMAX
#include <windows.h>

#include <array>
#include <vector>

struct ChildProcess::Impl
{
    PROCESS_INFORMATION ProcessInfo{};
    HANDLE StdInWrite = nullptr;
    HANDLE StdOutRead = nullptr;
    bool Running = false;
    std::string ReadBuffer;

    ~Impl()
    {
        Cleanup();
    }

    void Cleanup()
    {
        if (Running) {
            // Give the process a brief window to exit on its own (e.g. after the caller
            // already sent "quit") before forcefully killing it.
            if (WaitForSingleObject(ProcessInfo.hProcess, 500) != WAIT_OBJECT_0)
                TerminateProcess(ProcessInfo.hProcess, 1);

            WaitForSingleObject(ProcessInfo.hProcess, INFINITE);
            Running = false;
        }

        if (StdInWrite) {
            CloseHandle(StdInWrite);
            StdInWrite = nullptr;
        }

        if (StdOutRead) {
            CloseHandle(StdOutRead);
            StdOutRead = nullptr;
        }

        if (ProcessInfo.hProcess) {
            CloseHandle(ProcessInfo.hProcess);
            ProcessInfo.hProcess = nullptr;
        }

        if (ProcessInfo.hThread) {
            CloseHandle(ProcessInfo.hThread);
            ProcessInfo.hThread = nullptr;
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
    SECURITY_ATTRIBUTES securityAttributes{};
    securityAttributes.nLength = sizeof(SECURITY_ATTRIBUTES);
    securityAttributes.bInheritHandle = TRUE;
    securityAttributes.lpSecurityDescriptor = nullptr;

    HANDLE childStdInRead = nullptr;
    HANDLE childStdInWrite = nullptr;
    HANDLE childStdOutRead = nullptr;
    HANDLE childStdOutWrite = nullptr;

    if (!CreatePipe(&childStdInRead, &childStdInWrite, &securityAttributes, 0))
        return std::unexpected(EngineError{EngineErrorCode::ProcessLaunchFailed, "Failed to create stdin pipe"});

    if (!CreatePipe(&childStdOutRead, &childStdOutWrite, &securityAttributes, 0)) {
        CloseHandle(childStdInRead);
        CloseHandle(childStdInWrite);
        return std::unexpected(EngineError{EngineErrorCode::ProcessLaunchFailed, "Failed to create stdout pipe"});
    }

    // The parent only keeps the write end of stdin and the read end of stdout - strip
    // inheritance from those so they aren't leaked into further descendants.
    SetHandleInformation(childStdInWrite, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(childStdOutRead, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW startupInfo{};
    startupInfo.cb = sizeof(STARTUPINFOW);
    startupInfo.dwFlags = STARTF_USESTDHANDLES;
    startupInfo.hStdInput = childStdInRead;
    startupInfo.hStdOutput = childStdOutWrite;
    startupInfo.hStdError = childStdOutWrite;

    std::wstring commandLine = L"\"" + executablePath.wstring() + L"\"";
    for (const std::string& argument : arguments)
        commandLine += L" " + std::wstring(argument.begin(), argument.end());

    std::vector<wchar_t> commandLineBuffer(commandLine.begin(), commandLine.end());
    commandLineBuffer.push_back(L'\0');

    const std::wstring workingDirectory = executablePath.parent_path().wstring();

    PROCESS_INFORMATION processInfo{};

    const BOOL created = CreateProcessW(
        nullptr,
        commandLineBuffer.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        workingDirectory.empty() ? nullptr : workingDirectory.c_str(),
        &startupInfo,
        &processInfo);

    // Child-side handles must be closed in the parent regardless of outcome, or ReadFile
    // on the stdout pipe will block forever even after the child exits.
    CloseHandle(childStdInRead);
    CloseHandle(childStdOutWrite);

    if (!created) {
        CloseHandle(childStdInWrite);
        CloseHandle(childStdOutRead);
        return std::unexpected(EngineError{EngineErrorCode::ProcessLaunchFailed, "CreateProcessW failed with error " + std::to_string(GetLastError())});
    }

    m_Impl->ProcessInfo = processInfo;
    m_Impl->StdInWrite = childStdInWrite;
    m_Impl->StdOutRead = childStdOutRead;
    m_Impl->Running = true;

    return {};
}

bool ChildProcess::WriteLine(std::string_view line)
{
    if (!m_Impl->Running || !m_Impl->StdInWrite)
        return false;

    std::string buffer(line);
    buffer += '\n';

    DWORD bytesWritten = 0;
    const BOOL ok = WriteFile(m_Impl->StdInWrite, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesWritten, nullptr);

    return ok && bytesWritten == buffer.size();
}

std::optional<std::string> ChildProcess::ReadLine()
{
    if (!m_Impl->StdOutRead)
        return std::nullopt;

    for (;;) {
        const std::size_t newlinePos = m_Impl->ReadBuffer.find('\n');
        if (newlinePos != std::string::npos) {
            std::string line = m_Impl->ReadBuffer.substr(0, newlinePos);
            m_Impl->ReadBuffer.erase(0, newlinePos + 1);

            if (!line.empty() && line.back() == '\r')
                line.pop_back();

            return line;
        }

        std::array<char, 4096> chunk{};
        DWORD bytesRead = 0;
        const BOOL ok = ReadFile(m_Impl->StdOutRead, chunk.data(), static_cast<DWORD>(chunk.size()), &bytesRead, nullptr);

        if (!ok || bytesRead == 0)
            return std::nullopt;

        m_Impl->ReadBuffer.append(chunk.data(), bytesRead);
    }
}

bool ChildProcess::IsRunning() const
{
    if (!m_Impl->Running)
        return false;

    DWORD exitCode = 0;
    if (!GetExitCodeProcess(m_Impl->ProcessInfo.hProcess, &exitCode) || exitCode != STILL_ACTIVE) {
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
    if (!m_Impl->ProcessInfo.hProcess)
        return -1;

    WaitForSingleObject(m_Impl->ProcessInfo.hProcess, INFINITE);

    DWORD exitCode = 0;
    GetExitCodeProcess(m_Impl->ProcessInfo.hProcess, &exitCode);
    m_Impl->Running = false;

    return static_cast<int>(exitCode);
}
