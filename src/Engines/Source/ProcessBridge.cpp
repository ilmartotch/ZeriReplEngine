#include "../Include/ProcessBridge.h"
#include <array>
#include <filesystem>
#include <string>
#include <string_view>

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
        class ScopedHandle final {
        public:
            ScopedHandle() = default;
            explicit ScopedHandle(HANDLE h) noexcept : m_handle(h) {}
            ScopedHandle(const ScopedHandle&) = delete;
            ScopedHandle& operator=(const ScopedHandle&) = delete;

            ScopedHandle(ScopedHandle&& other) noexcept : m_handle(other.m_handle) {
                other.m_handle = nullptr;
            }

            ScopedHandle& operator=(ScopedHandle&& other) noexcept {
                if (this != &other) {
                    Reset();
                    m_handle = other.m_handle;
                    other.m_handle = nullptr;
                }
                return *this;
            }

            ~ScopedHandle() { Reset(); }

            [[nodiscard]] HANDLE Get() const noexcept { return m_handle; }

            [[nodiscard]] HANDLE Release() noexcept {
                HANDLE tmp = m_handle;
                m_handle = nullptr;
                return tmp;
            }

            void Reset(HANDLE h = nullptr) noexcept {
                if (m_handle != nullptr) {
                    CloseHandle(m_handle);
                }
                m_handle = h;
            }

        private:
            HANDLE m_handle{ nullptr };
        };

        [[nodiscard]] std::wstring Utf8ToWide(std::string_view input) {
            if (input.empty()) return {};
            const int size = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                input.data(),
                static_cast<int>(input.size()),
                nullptr,
                0
            );

            if (size <= 0) return {};

            std::wstring out(static_cast<size_t>(size), L'\0');
            const int written = MultiByteToWideChar(
                CP_UTF8,
                MB_ERR_INVALID_CHARS,
                input.data(),
                static_cast<int>(input.size()),
                out.data(),
                size
            );

            if (written <= 0) return {};
            return out;
        }

        [[nodiscard]] std::wstring QuoteArgument(const std::wstring& arg) {
            if (arg.find_first_of(L" \t\"") == std::wstring::npos) return arg;

            std::wstring out;
            out.push_back(L'"');

            size_t backslashes = 0;
            for (wchar_t ch : arg) {
                if (ch == L'\\') {
                    ++backslashes;
                } else if (ch == L'"') {
                    out.append(backslashes * 2 + 1, L'\\');
                    out.push_back(L'"');
                    backslashes = 0;
                } else {
                    if (backslashes > 0) {
                        out.append(backslashes, L'\\');
                        backslashes = 0;
                    }
                    out.push_back(ch);
                }
            }

            if (backslashes > 0) out.append(backslashes * 2, L'\\');
            out.push_back(L'"');
            return out;
        }
    }

    ExecutionOutcome ProcessBridge::Run(
        const std::filesystem::path& executablePath,
        const std::vector<std::string>& args,
        OutputCallback onOutput,
        const std::optional<std::filesystem::path>& cwd
    ) {
        if (m_running) {
            return std::unexpected(ExecutionError{ "STATE_ERR", "A process is already running." });
        }

        SECURITY_ATTRIBUTES saAttr{};
        saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
        saAttr.bInheritHandle = TRUE;
        saAttr.lpSecurityDescriptor = nullptr;

        ScopedHandle outRead;
        ScopedHandle outWrite;
        ScopedHandle inRead;
        ScopedHandle inWrite;
        ScopedHandle job;

        HANDLE outReadRaw = nullptr;
        HANDLE outWriteRaw = nullptr;
        if (!CreatePipe(&outReadRaw, &outWriteRaw, &saAttr, 0)) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stdout pipe." });
        }
        outRead.Reset(outReadRaw);
        outWrite.Reset(outWriteRaw);

        if (!SetHandleInformation(outRead.Get(), HANDLE_FLAG_INHERIT, 0)) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to set stdout pipe inheritance." });
        }

        HANDLE inReadRaw = nullptr;
        HANDLE inWriteRaw = nullptr;
        if (!CreatePipe(&inReadRaw, &inWriteRaw, &saAttr, 0)) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stdin pipe." });
        }
        inRead.Reset(inReadRaw);
        inWrite.Reset(inWriteRaw);

        if (!SetHandleInformation(inWrite.Get(), HANDLE_FLAG_INHERIT, 0)) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to set stdin pipe inheritance." });
        }

        job.Reset(CreateJobObjectW(nullptr, nullptr));
        if (!job.Get()) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create Job Object." });
        }

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to configure Job Object." });
        }

        const std::string pathStr = executablePath.string();
        const std::wstring widePath = Utf8ToWide(pathStr);
        if (widePath.empty() && !pathStr.empty()) {
            return std::unexpected(ExecutionError{ "UTF8_ERR", "Invalid UTF-8 executable path." });
        }

        std::wstring cmdLine = QuoteArgument(widePath);
        for (const auto& arg : args) {
            const std::wstring wideArg = Utf8ToWide(arg);
            if (wideArg.empty() && !arg.empty()) {
                return std::unexpected(ExecutionError{ "UTF8_ERR", "Invalid UTF-8 argument." });
            }
            cmdLine.push_back(L' ');
            cmdLine.append(QuoteArgument(wideArg));
        }

        // Resolve optional cwd to wide string for CreateProcessW
        const wchar_t* wideCwd = nullptr;
        std::wstring cwdWide;
        if (cwd.has_value()) {
            cwdWide = cwd->wstring();
            wideCwd = cwdWide.c_str();
        }

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        si.hStdError = outWrite.Get();
        si.hStdOutput = outWrite.Get();
        si.hStdInput = inRead.Get();
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE, CREATE_SUSPENDED, nullptr, wideCwd, &si, &pi)) {
            return std::unexpected(ExecutionError{ "LAUNCH_ERR", "CreateProcessW failed." });
        }

        ScopedHandle process(pi.hProcess);
        ScopedHandle thread(pi.hThread);

        if (!AssignProcessToJobObject(job.Get(), process.Get())) {
            TerminateProcess(process.Get(), 1);
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to assign process to Job Object." });
        }

        ResumeThread(thread.Get());

        m_hStdOutRead = outRead.Release();
        m_hStdInWrite = inWrite.Release();
        m_jobObject = job.Release();
        m_hChildProcess = process.Release();

        // Parent-side closures
        outWrite.Reset();
        inRead.Reset();
        thread.Reset();

        m_running = true;
        m_outputThread = std::jthread(&ProcessBridge::ReadOutputLoop, this, std::move(onOutput));
        return "Process started.";
    }

    void ProcessBridge::ReadOutputLoop(OutputCallback onOutput) {
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        while (m_running && m_hStdOutRead &&
               ReadFile(m_hStdOutRead, buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, nullptr) &&
               read > 0) {
            buffer[read] = '\0';
            onOutput(std::string(buffer.data(), buffer.data() + read));
        }
        m_running = false;
    }

    void ProcessBridge::SendInput(const std::string& input) {
        if (!m_running || !m_hStdInWrite) return;
        const std::string payload = input + "\n";
        DWORD written = 0;
        (void)WriteFile(m_hStdInWrite, payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    }

    void ProcessBridge::Terminate() {
        m_running = false;

        if (m_hStdInWrite) { CloseHandle(m_hStdInWrite); m_hStdInWrite = nullptr; }
        if (m_hStdOutRead) { CloseHandle(m_hStdOutRead); m_hStdOutRead = nullptr; }

        if (m_jobObject) {
            CloseHandle(m_jobObject); // kill-on-close
            m_jobObject = nullptr;
        }

        if (m_hChildProcess) {
            TerminateProcess(m_hChildProcess, 0);
            CloseHandle(m_hChildProcess);
            m_hChildProcess = nullptr;
        }
    }

#else
    namespace {
        class ScopedFd final {
        public:
            ScopedFd() = default;
            explicit ScopedFd(int fd) noexcept : m_fd(fd) {}
            ScopedFd(const ScopedFd&) = delete;
            ScopedFd& operator=(const ScopedFd&) = delete;

            ScopedFd(ScopedFd&& other) noexcept : m_fd(other.m_fd) {
                other.m_fd = -1;
            }

            ScopedFd& operator=(ScopedFd&& other) noexcept {
                if (this != &other) {
                    Reset();
                    m_fd = other.m_fd;
                    other.m_fd = -1;
                }
                return *this;
            }

            ~ScopedFd() { Reset(); }

            [[nodiscard]] int Get() const noexcept { return m_fd; }

            [[nodiscard]] int Release() noexcept {
                const int out = m_fd;
                m_fd = -1;
                return out;
            }

            void Reset(int fd = -1) noexcept {
                if (m_fd != -1) {
                    close(m_fd);
                }
                m_fd = fd;
            }

        private:
            int m_fd{ -1 };
        };

        void SigChldHandler(int) {
            const int saved = errno;
            while (waitpid(-1, nullptr, WNOHANG) > 0) {}
            errno = saved;
        }

        void InstallSigChldHandler() {
            static std::once_flag once;
            std::call_once(once, []() {
                struct sigaction sa{};
                sa.sa_handler = SigChldHandler;
                sigemptyset(&sa.sa_mask);
                sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
                sigaction(SIGCHLD, &sa, nullptr);
            });
        }
    }

    ExecutionOutcome ProcessBridge::Run(
        const std::filesystem::path& executablePath,
        const std::vector<std::string>& args,
        OutputCallback onOutput,
        const std::optional<std::filesystem::path>& cwd
    ) {
        if (m_running) {
            return std::unexpected(ExecutionError{ "STATE_ERR", "A process is already running." });
        }

        int outPipe[2]{ -1, -1 };
        int inPipe[2]{ -1, -1 };

        if (pipe(outPipe) != 0) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stdout pipe." });
        }
        ScopedFd outRead(outPipe[0]);
        ScopedFd outWrite(outPipe[1]);

        if (pipe(inPipe) != 0) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stdin pipe." });
        }
        ScopedFd inRead(inPipe[0]);
        ScopedFd inWrite(inPipe[1]);

        InstallSigChldHandler();

        const std::string pathStr = executablePath.string();

        const pid_t pid = fork();
        if (pid < 0) {
            return std::unexpected(ExecutionError{ "OS_ERR", "fork() failed." });
        }

        if (pid == 0) {
#ifdef __linux__
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (getppid() == 1) _exit(1);
#endif
            // Change working directory if requested
            if (cwd.has_value()) {
                if (chdir(cwd->string().c_str()) != 0) _exit(126);
            }

            dup2(outWrite.Get(), STDOUT_FILENO);
            dup2(outWrite.Get(), STDERR_FILENO);
            dup2(inRead.Get(), STDIN_FILENO);

            outRead.Reset();
            outWrite.Reset();
            inRead.Reset();
            inWrite.Reset();

            std::vector<char*> argv;
            argv.reserve(args.size() + 2);
            argv.push_back(const_cast<char*>(pathStr.c_str()));
            for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
            argv.push_back(nullptr);

            execvp(pathStr.c_str(), argv.data());
            _exit(127);
        }

        m_childPid = static_cast<int>(pid);

        // parent keeps read stdout + write stdin
        m_stdoutPipe[0] = outRead.Release();
        m_stdoutPipe[1] = -1;
        m_stdinPipe[0] = -1;
        m_stdinPipe[1] = inWrite.Release();

        outWrite.Reset();
        inRead.Reset();

        m_running = true;
        m_outputThread = std::jthread(&ProcessBridge::ReadOutputLoop, this, std::move(onOutput));
        return "Process started.";
    }

    void ProcessBridge::ReadOutputLoop(OutputCallback onOutput) {
        std::array<char, 4096> buffer{};
        while (m_running && m_stdoutPipe[0] != -1) {
            const ssize_t bytesRead = read(m_stdoutPipe[0], buffer.data(), buffer.size() - 1);
            if (bytesRead > 0) {
                buffer[static_cast<size_t>(bytesRead)] = '\0';
                onOutput(std::string(buffer.data(), static_cast<size_t>(bytesRead)));
                continue;
            }

            if (bytesRead < 0 && errno == EINTR) {
                continue;
            }

            break;
        }
        m_running = false;
    }

    void ProcessBridge::SendInput(const std::string& input) {
        if (!m_running || m_stdinPipe[1] == -1) return;

        const std::string payload = input + "\n";
        const char* data = payload.data();
        size_t remaining = payload.size();

        while (remaining > 0) {
            const ssize_t written = write(m_stdinPipe[1], data, remaining);
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

        if (m_stdinPipe[1] != -1) { close(m_stdinPipe[1]); m_stdinPipe[1] = -1; }
        if (m_stdoutPipe[0] != -1) { close(m_stdoutPipe[0]); m_stdoutPipe[0] = -1; }

        if (m_childPid > 0) {
            kill(m_childPid, SIGTERM);
            (void)waitpid(m_childPid, nullptr, 0);
            m_childPid = -1;
        }
    }
#endif

    int ProcessBridge::ExecuteSync(
        const std::filesystem::path& executablePath,
        const std::vector<std::string>& args,
        const std::optional<std::filesystem::path>& cwd
    ) {
#ifdef _WIN32
        const std::string pathStr = executablePath.string();
        const std::wstring widePath = Utf8ToWide(pathStr);
        if (widePath.empty() && !pathStr.empty()) return -1;

        std::wstring cmdLine = QuoteArgument(widePath);
        for (const auto& arg : args) {
            const std::wstring wideArg = Utf8ToWide(arg);
            if (wideArg.empty() && !arg.empty()) return -1;
            cmdLine.push_back(L' ');
            cmdLine.append(QuoteArgument(wideArg));
        }

        const wchar_t* wideCwd = nullptr;
        std::wstring cwdWide;
        if (cwd.has_value()) {
            cwdWide = cwd->wstring();
            wideCwd = cwdWide.c_str();
        }

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        si.dwFlags = STARTF_USESHOWWINDOW;
        si.wShowWindow = SW_HIDE;

        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(
            nullptr,
            cmdLine.data(),
            nullptr,
            nullptr,
            FALSE,
            CREATE_NO_WINDOW,
            nullptr,
            wideCwd,
            &si,
            &pi
        )) {
            return -1;
        }

        ScopedHandle process(pi.hProcess);
        ScopedHandle thread(pi.hThread);

        WaitForSingleObject(process.Get(), INFINITE);

        DWORD exitCode = 0;
        GetExitCodeProcess(process.Get(), &exitCode);

        return static_cast<int>(exitCode);
#else
        const std::string pathStr = executablePath.string();

        pid_t pid = fork();
        if (pid == -1) {
            return -1;
        }

        if (pid == 0) {
            // Change working directory if requested
            if (cwd.has_value()) {
                if (chdir(cwd->string().c_str()) != 0) _exit(126);
            }

            std::vector<char*> argv;
            argv.push_back(const_cast<char*>(pathStr.c_str()));
            for (const auto& arg : args) {
                argv.push_back(const_cast<char*>(arg.c_str()));
            }
            argv.push_back(nullptr);

            execvp(pathStr.c_str(), argv.data());
            _exit(127);
        }

        int status = 0;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            return WEXITSTATUS(status);
        }

        return -1;
#endif
}

}

/*
The Windows version uses Anonymous Pipes to capture the child process's streams.
It launches the process using CreateProcessW and monitors the output in a dedicated thread.
Sending input is performed by writing to the write-end of the input pipe.
This architecture allows the REPL to remain responsive while the child process runs.
*/
