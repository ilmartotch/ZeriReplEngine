#include "../Include/ProcessBridge.h"
#include <iostream>
#include <array>

namespace Zeri::Engines::Defaults {

    ProcessBridge::ProcessBridge() = default;

    ProcessBridge::~ProcessBridge() {
        Terminate();
    }

#ifdef _WIN32
    ExecutionOutcome ProcessBridge::Run(const std::string& path, const std::vector<std::string>& args, OutputCallback onOutput) {
        SECURITY_ATTRIBUTES saAttr;
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = NULL;

        // Create Pipes for STDOUT and STDIN
        HANDLE hStdOutWrite;
        if (!CreatePipe(&m_hStdOutRead, &hStdOutWrite, &saAttr, 0)) return std::unexpected(ExecutionError{"OS_ERR", "Failed Out Pipe"});
        if (!SetHandleInformation(m_hStdOutRead, HANDLE_FLAG_INHERIT, 0)) return std::unexpected(ExecutionError{"OS_ERR", "Failed Pipe Info"});

        HANDLE hStdInRead;
        if (!CreatePipe(&hStdInRead, &m_hStdInWrite, &saAttr, 0)) return std::unexpected(ExecutionError{"OS_ERR", "Failed In Pipe"});
        if (!SetHandleInformation(m_hStdInWrite, HANDLE_FLAG_INHERIT, 0)) return std::unexpected(ExecutionError{"OS_ERR", "Failed Pipe Info"});

        PROCESS_INFORMATION piProcInfo;
        STARTUPINFOA siStartInfo;
        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
        ZeroMemory(&siStartInfo, sizeof(STARTUPINFOA));
        siStartInfo.cb = sizeof(STARTUPINFOA);
        siStartInfo.hStdError = hStdOutWrite;
        siStartInfo.hStdOutput = hStdOutWrite;
        siStartInfo.hStdInput = hStdInRead;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        std::string cmdLine = path;
        for (const auto& arg : args) cmdLine += " " + arg;

        if (!CreateProcessA(NULL, cmdLine.data(), NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo)) {
            return std::unexpected(ExecutionError{"LAUNCH_ERR", "Process creation failed."});
        }

        m_hChildProcess = piProcInfo.hProcess;
        CloseHandle(piProcInfo.hThread);
        CloseHandle(hStdOutWrite);
        CloseHandle(hStdInRead);

        m_running = true;
        m_outputThread = std::jthread(&ProcessBridge::ReadOutputLoop, this, onOutput);

        return "Process started.";
    }

    void ProcessBridge::ReadOutputLoop(OutputCallback onOutput) {
        std::array<char, 4096> buffer;
        DWORD dwRead;
        while (m_running && ReadFile(m_hStdOutRead, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &dwRead, NULL) && dwRead > 0) {
            buffer[dwRead] = '\0';
            onOutput(std::string(buffer.data()));
        }
        m_running = false;
    }

    void ProcessBridge::SendInput(const std::string& input) {
        if (!m_running || !m_hStdInWrite) return;
        DWORD dwWritten;
        std::string payload = input + "\n";
        WriteFile(m_hStdInWrite, payload.c_str(), static_cast<DWORD>(payload.length()), &dwWritten, NULL);
    }

    void ProcessBridge::Terminate() {
        m_running = false;
        if (m_hChildProcess) {
            TerminateProcess(m_hChildProcess, 0);
            CloseHandle(m_hChildProcess);
            m_hChildProcess = nullptr;
        }
        if (m_hStdInWrite) { CloseHandle(m_hStdInWrite); m_hStdInWrite = nullptr; }
        if (m_hStdOutRead) { CloseHandle(m_hStdOutRead); m_hStdOutRead = nullptr; }
    }
#else
    // POSIX Implementation (Linux/macOS)
    ExecutionOutcome ProcessBridge::Run(const std::string& path, const std::vector<std::string>& args, OutputCallback onOutput) {
        // Here we would use pipe(), fork(), and execvp()
        return std::unexpected(ExecutionError{"NOT_IMPL", "POSIX bridge implementation pending."});
    }
    void ProcessBridge::ReadOutputLoop(OutputCallback onOutput) {}
    void ProcessBridge::SendInput(const std::string& input) {}
    void ProcessBridge::Terminate() {}
#endif

}

/*
FILE DOCUMENTATION:
ProcessBridge Implementation.
The Windows version uses Anonymous Pipes to capture the child process's streams.
It launches the process using CreateProcessA and monitors the output in a dedicated thread.
Sending input is performed by writing to the write-end of the input pipe.
This architecture allows the REPL to remain responsive while the child process runs.
*/
