#include "../Include/ProcessBridge.h"
#include <iostream>
#include <array>
#include <string>
#include <string_view>

namespace Zeri::Engines::Defaults {

    ProcessBridge::ProcessBridge() = default;

    ProcessBridge::~ProcessBridge() {
        Terminate();
    }

#ifdef _WIN32
    namespace {
        std::wstring Utf8ToWide(std::string_view input) {
            if (input.empty()) return {};
            int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
            if (size <= 0) return {};
            std::wstring wide(static_cast<size_t>(size), L'\0');
            MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), wide.data(), size);
            return wide;
        }

        std::wstring QuoteArgument(const std::wstring& arg) {
            if (arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;

            std::wstring quoted;
            quoted.push_back(L'"');

            size_t backslashes = 0;
            for (wchar_t ch : arg) {
                if (ch == L'\\') {
                    ++backslashes;
                } else if (ch == L'"') {
                    quoted.append(backslashes * 2 + 1, L'\\');
                    quoted.push_back(L'"');
                    backslashes = 0;
                } else {
                    if (backslashes > 0) {
                        quoted.append(backslashes, L'\\');
                        backslashes = 0;
                    }
                    quoted.push_back(ch);
                }
            }

            if (backslashes > 0) quoted.append(backslashes * 2, L'\\');
            quoted.push_back(L'"');
            return quoted;
        }
    }

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
        STARTUPINFOW siStartInfo;
        ZeroMemory(&piProcInfo, sizeof(PROCESS_INFORMATION));
        ZeroMemory(&siStartInfo, sizeof(STARTUPINFOW));
        siStartInfo.cb = sizeof(STARTUPINFOW);
        siStartInfo.hStdError = hStdOutWrite;
        siStartInfo.hStdOutput = hStdOutWrite;
        siStartInfo.hStdInput = hStdInRead;
        siStartInfo.dwFlags |= STARTF_USESTDHANDLES;

        std::wstring widePath = Utf8ToWide(path);
        if (widePath.empty() && !path.empty()) {
            return std::unexpected(ExecutionError{"UTF8_ERR", "Invalid UTF-8 executable path."});
        }

        std::wstring cmdLine = QuoteArgument(widePath);
        for (const auto& arg : args) {
            std::wstring wideArg = Utf8ToWide(arg);
            if (wideArg.empty() && !arg.empty()) {
                return std::unexpected(ExecutionError{"UTF8_ERR", "Invalid UTF-8 argument."});
            }
            cmdLine.append(L" ");
            cmdLine.append(QuoteArgument(wideArg));
        }

        if (!CreateProcessW(NULL, cmdLine.data(), NULL, NULL, TRUE, 0, NULL, NULL, &siStartInfo, &piProcInfo)) {
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
The Windows version uses Anonymous Pipes to capture the child process's streams.
It launches the process using CreateProcessW and monitors the output in a dedicated thread.
Sending input is performed by writing to the write-end of the input pipe.
This architecture allows the REPL to remain responsive while the child process runs.
*/
