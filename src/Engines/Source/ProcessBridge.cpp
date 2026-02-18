#include "../Include/ProcessBridge.h"
#include <iostream>
#include <array>
#include <string>
#include <string_view>
#include <mutex>

#ifndef _WIN32
    #include <cerrno>
    #include <csignal>
    #include <sys/wait.h>
    #ifdef __linux__
        #include <sys/prctl.h>
    #endif
#endif

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
        HANDLE hStdOutWrite = nullptr;
        if (!CreatePipe(&m_hStdOutRead, &hStdOutWrite, &saAttr, 0)) return std::unexpected(ExecutionError{"OS_ERR", "Failed Out Pipe"});
        if (!SetHandleInformation(m_hStdOutRead, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            m_hStdOutRead = nullptr;
            return std::unexpected(ExecutionError{"OS_ERR", "Failed Pipe Info"});
        }

        HANDLE hStdInRead = nullptr;
        if (!CreatePipe(&hStdInRead, &m_hStdInWrite, &saAttr, 0)) {
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            m_hStdOutRead = nullptr;
            return std::unexpected(ExecutionError{"OS_ERR", "Failed In Pipe"});
        }
        if (!SetHandleInformation(m_hStdInWrite, HANDLE_FLAG_INHERIT, 0)) {
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdInRead);
            CloseHandle(m_hStdInWrite);
            m_hStdOutRead = nullptr;
            m_hStdInWrite = nullptr;
            return std::unexpected(ExecutionError{"OS_ERR", "Failed Pipe Info"});
        }

        m_jobObject = CreateJobObjectW(nullptr, nullptr);
        if (!m_jobObject) {
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdInRead);
            CloseHandle(m_hStdInWrite);
            m_hStdOutRead = nullptr;
            m_hStdInWrite = nullptr;
            return std::unexpected(ExecutionError{"OS_ERR", "Failed to create Job Object"});
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(m_jobObject, JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
            CloseHandle(m_jobObject);
            m_jobObject = nullptr;
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdInRead);
            CloseHandle(m_hStdInWrite);
            m_hStdOutRead = nullptr;
            m_hStdInWrite = nullptr;
            return std::unexpected(ExecutionError{"OS_ERR", "Failed to configure Job Object"});
        }

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
            CloseHandle(m_jobObject);
            m_jobObject = nullptr;
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdInRead);
            CloseHandle(m_hStdInWrite);
            m_hStdOutRead = nullptr;
            m_hStdInWrite = nullptr;
            return std::unexpected(ExecutionError{"UTF8_ERR", "Invalid UTF-8 executable path."});
        }

        std::wstring cmdLine = QuoteArgument(widePath);
        for (const auto& arg : args) {
            std::wstring wideArg = Utf8ToWide(arg);
            if (wideArg.empty() && !arg.empty()) {
                CloseHandle(m_jobObject);
                m_jobObject = nullptr;
                CloseHandle(m_hStdOutRead);
                CloseHandle(hStdOutWrite);
                CloseHandle(hStdInRead);
                CloseHandle(m_hStdInWrite);
                m_hStdOutRead = nullptr;
                m_hStdInWrite = nullptr;
                return std::unexpected(ExecutionError{"UTF8_ERR", "Invalid UTF-8 argument."});
            }
            cmdLine.append(L" ");
            cmdLine.append(QuoteArgument(wideArg));
        }

        DWORD creationFlags = CREATE_SUSPENDED;
        if (!CreateProcessW(NULL, cmdLine.data(), NULL, NULL, TRUE, creationFlags, NULL, NULL, &siStartInfo, &piProcInfo)) {
            CloseHandle(m_jobObject);
            m_jobObject = nullptr;
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdInRead);
            CloseHandle(m_hStdInWrite);
            m_hStdOutRead = nullptr;
            m_hStdInWrite = nullptr;
            return std::unexpected(ExecutionError{"LAUNCH_ERR", "Process creation failed."});
        }

        if (!AssignProcessToJobObject(m_jobObject, piProcInfo.hProcess)) {
            TerminateProcess(piProcInfo.hProcess, 0);
            CloseHandle(piProcInfo.hThread);
            CloseHandle(piProcInfo.hProcess);
            CloseHandle(m_jobObject);
            m_jobObject = nullptr;
            CloseHandle(m_hStdOutRead);
            CloseHandle(hStdOutWrite);
            CloseHandle(hStdInRead);
            CloseHandle(m_hStdInWrite);
            m_hStdOutRead = nullptr;
            m_hStdInWrite = nullptr;
            return std::unexpected(ExecutionError{"OS_ERR", "Failed to assign process to Job Object"});
        }

        ResumeThread(piProcInfo.hThread);

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
        if (m_jobObject) {
            CloseHandle(m_jobObject);
            m_jobObject = nullptr;
        }
        if (m_hChildProcess) {
            TerminateProcess(m_hChildProcess, 0);
            CloseHandle(m_hChildProcess);
            m_hChildProcess = nullptr;
        }
        if (m_hStdInWrite) { CloseHandle(m_hStdInWrite); m_hStdInWrite = nullptr; }
        if (m_hStdOutRead) { CloseHandle(m_hStdOutRead); m_hStdOutRead = nullptr; }
    }
#else
    namespace {
        void SigChldHandler(int) {
            int savedErrno = errno;
            while (waitpid(-1, nullptr, WNOHANG) > 0) {}
            errno = savedErrno;
        }

        void InstallSigChldHandler() {
            static std::once_flag flag;
            std::call_once(flag, []() {
                struct sigaction sa {};
                sa.sa_handler = SigChldHandler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
                sigaction(SIGCHLD, &sa, nullptr);
            });
        }
    }

    ExecutionOutcome ProcessBridge::Run(const std::string& path, const std::vector<std::string>& args, OutputCallback onOutput) {
        if (pipe(m_stdoutPipe) != 0) return std::unexpected(ExecutionError{"OS_ERR", "Failed Out Pipe"});
        if (pipe(m_stdinPipe) != 0) {
            close(m_stdoutPipe[0]);
            close(m_stdoutPipe[1]);
            m_stdoutPipe[0] = -1;
            m_stdoutPipe[1] = -1;
            return std::unexpected(ExecutionError{"OS_ERR", "Failed In Pipe"});
        }

        InstallSigChldHandler();

        pid_t pid = fork();
        if (pid < 0) {
            close(m_stdoutPipe[0]);
            close(m_stdoutPipe[1]);
            close(m_stdinPipe[0]);
            close(m_stdinPipe[1]);
            m_stdoutPipe[0] = -1;
            m_stdoutPipe[1] = -1;
            m_stdinPipe[0] = -1;
            m_stdinPipe[1] = -1;
            return std::unexpected(ExecutionError{"OS_ERR", "fork() failed"});
        }

        if (pid == 0) {
            #ifdef __linux__
                prctl(PR_SET_PDEATHSIG, SIGTERM);
                if (getppid() == 1) _exit(1);
            #endif

            dup2(m_stdoutPipe[1], STDOUT_FILENO);
            dup2(m_stdoutPipe[1], STDERR_FILENO);
            dup2(m_stdinPipe[0], STDIN_FILENO);

            close(m_stdoutPipe[0]);
            close(m_stdoutPipe[1]);
            close(m_stdinPipe[0]);
            close(m_stdinPipe[1]);

            std::vector<char*> argv;
            argv.reserve(args.size() + 2);
            argv.push_back(const_cast<char*>(path.c_str()));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execvp(path.c_str(), argv.data());
            _exit(127);
        }

        m_childPid = static_cast<int>(pid);

        close(m_stdoutPipe[1]);
        close(m_stdinPipe[0]);
        m_stdoutPipe[1] = -1;
        m_stdinPipe[0] = -1;

        m_running = true;
        m_outputThread = std::jthread(&ProcessBridge::ReadOutputLoop, this, onOutput);

        return "Process started.";
    }

    void ProcessBridge::ReadOutputLoop(OutputCallback onOutput) {
        std::array<char, 4096> buffer{};
        while (m_running) {
            ssize_t bytesRead = read(m_stdoutPipe[0], buffer.data(), buffer.size() - 1);
            if (bytesRead > 0) {
                buffer[static_cast<size_t>(bytesRead)] = '\0';
                onOutput(std::string(buffer.data(), static_cast<size_t>(bytesRead)));
                continue;
            }
            if (bytesRead < 0 && errno == EINTR) continue;
            break;
        }
        m_running = false;
    }

    void ProcessBridge::SendInput(const std::string& input) {
        if (!m_running || m_stdinPipe[1] == -1) return;
        std::string payload = input + "\n";
        const char* data = payload.c_str();
        size_t remaining = payload.size();
        while (remaining > 0) {
            ssize_t written = write(m_stdinPipe[1], data, remaining);
            if (written > 0) {
                data += written;
                remaining -= static_cast<size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR) continue;
            break;
        }
    }

    void ProcessBridge::Terminate() {
        m_running = false;
        if (m_childPid > 0) {
            kill(m_childPid, SIGTERM);
            waitpid(m_childPid, nullptr, WNOHANG);
            m_childPid = -1;
        }
        if (m_stdinPipe[1] != -1) { close(m_stdinPipe[1]); m_stdinPipe[1] = -1; }
        if (m_stdoutPipe[0] != -1) { close(m_stdoutPipe[0]); m_stdoutPipe[0] = -1; }
    }
#endif

}

/*
The Windows version uses Anonymous Pipes to capture the child process's streams.
It launches the process using CreateProcessW and monitors the output in a dedicated thread.
Sending input is performed by writing to the write-end of the input pipe.
This architecture allows the REPL to remain responsive while the child process runs.
*/
