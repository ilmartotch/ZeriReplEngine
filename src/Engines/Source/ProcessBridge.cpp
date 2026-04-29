#include "../Include/ProcessBridge.h"
#include "../../ZeriLink/Include/ScopedHandle.h"
#include <array>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <cerrno>
    #include <csignal>
    #include <unistd.h>
    #include <sys/wait.h>
    #ifdef __linux__
        #include <sys/prctl.h>
    #endif
#endif

namespace Zeri::Engines::Defaults {

    void ProcessBridge::JoinIoThreads() {
        if (m_outputThread.joinable()) {
            m_outputThread.join();
        }
        if (m_errorThread.joinable()) {
            m_errorThread.join();
        }
    }

    struct ProcessBridge::Impl {
#ifdef _WIN32
        Zeri::Link::ScopedHandle childProcess;
        Zeri::Link::ScopedHandle stdInWrite;
        Zeri::Link::ScopedHandle stdOutRead;
        Zeri::Link::ScopedHandle stdErrRead;
        Zeri::Link::ScopedHandle jobObject;
#else
        int childPid{ -1 };
        Zeri::Link::ScopedFd stdinWrite;
        Zeri::Link::ScopedFd stdoutRead;
        Zeri::Link::ScopedFd stderrRead;
#endif
    };

    ProcessBridge::ProcessBridge() : m_impl(std::make_unique<Impl>()) {}

    ProcessBridge::~ProcessBridge() {
        Terminate();
    }

#ifdef _WIN32
    namespace {
        using Zeri::Link::ScopedHandle;

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
        ErrorCallback onError,
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
        ScopedHandle errRead;
        ScopedHandle errWrite;
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

        HANDLE errReadRaw = nullptr;
        HANDLE errWriteRaw = nullptr;
        if (!CreatePipe(&errReadRaw, &errWriteRaw, &saAttr, 0)) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stderr pipe." });
        }
        errRead.Reset(errReadRaw);
        errWrite.Reset(errWriteRaw);

        if (!SetHandleInformation(errRead.Get(), HANDLE_FLAG_INHERIT, 0)) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to set stderr pipe inheritance." });
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

        const wchar_t* wideCwd = nullptr;
        std::wstring cwdWide;
        if (cwd.has_value()) {
            cwdWide = cwd->wstring();
            wideCwd = cwdWide.c_str();
        }

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        si.hStdError = errWrite.Get();
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

        m_impl->stdOutRead.Reset(outRead.Release());
        m_impl->stdErrRead.Reset(errRead.Release());
        m_impl->stdInWrite.Reset(inWrite.Release());
        m_impl->jobObject.Reset(job.Release());
        m_impl->childProcess.Reset(process.Release());

        outWrite.Reset();
        errWrite.Reset();
        inRead.Reset();
        thread.Reset();

        m_lastExitCode = -1;
        m_activeReadLoops = 2;
        m_running = true;
        m_outputThread = std::thread(&ProcessBridge::ReadOutputLoop, this, std::move(onOutput));
        m_errorThread = std::thread(&ProcessBridge::ReadErrorLoop, this, std::move(onError));
        return "Process started.";
    }

    void ProcessBridge::ReadOutputLoop(OutputCallback onOutput) {
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        while (m_running && m_impl->stdOutRead.IsValid() &&
               ReadFile(m_impl->stdOutRead.Get(), buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, nullptr) &&
               read > 0) {
            buffer[read] = '\0';
            onOutput(std::string(buffer.data(), buffer.data() + read));
        }
        if (m_activeReadLoops.fetch_sub(1) == 1) {
            m_running = false;
        }
    }

    void ProcessBridge::ReadErrorLoop(ErrorCallback onError) {
        std::array<char, 4096> buffer{};
        DWORD read = 0;
        while (m_running && m_impl->stdErrRead.IsValid() &&
               ReadFile(m_impl->stdErrRead.Get(), buffer.data(), static_cast<DWORD>(buffer.size() - 1), &read, nullptr) &&
               read > 0) {
            buffer[read] = '\0';
            onError(std::string(buffer.data(), buffer.data() + read));
        }
        if (m_activeReadLoops.fetch_sub(1) == 1) {
            m_running = false;
        }
    }

    void ProcessBridge::SendInput(const std::string& input) {
        if (!m_running || !m_impl->stdInWrite.IsValid()) return;
        const std::string payload = input + "\n";
        DWORD written = 0;
        (void)WriteFile(m_impl->stdInWrite.Get(), payload.data(), static_cast<DWORD>(payload.size()), &written, nullptr);
    }

    void ProcessBridge::Terminate() {
        m_running = false;

        m_impl->stdInWrite.Reset();
        m_impl->stdOutRead.Reset();
        m_impl->stdErrRead.Reset();

        if (m_impl->jobObject.IsValid()) {
            m_impl->jobObject.Reset();
        }

        if (m_impl->childProcess.IsValid()) {
            TerminateProcess(m_impl->childProcess.Get(), 0);
            (void)WaitForSingleObject(m_impl->childProcess.Get(), INFINITE);
            m_lastExitCode = 0;
            m_impl->childProcess.Reset();
        }

        JoinIoThreads();
    }

#else
    namespace {
        using Zeri::Link::ScopedFd;
    }

    ExecutionOutcome ProcessBridge::Run(
        const std::filesystem::path& executablePath,
        const std::vector<std::string>& args,
        OutputCallback onOutput,
        ErrorCallback onError,
        const std::optional<std::filesystem::path>& cwd
    ) {
        if (m_running) {
            return std::unexpected(ExecutionError{ "STATE_ERR", "A process is already running." });
        }

        int outPipe[2]{ -1, -1 };
        int errPipe[2]{ -1, -1 };
        int inPipe[2]{ -1, -1 };

        if (pipe(outPipe) != 0) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stdout pipe." });
        }
        ScopedFd outRead(outPipe[0]);
        ScopedFd outWrite(outPipe[1]);

        if (pipe(errPipe) != 0) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stderr pipe." });
        }
        ScopedFd errRead(errPipe[0]);
        ScopedFd errWrite(errPipe[1]);

        if (pipe(inPipe) != 0) {
            return std::unexpected(ExecutionError{ "OS_ERR", "Failed to create stdin pipe." });
        }
        ScopedFd inRead(inPipe[0]);
        ScopedFd inWrite(inPipe[1]);

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
            if (cwd.has_value()) {
                if (chdir(cwd->string().c_str()) != 0) _exit(126);
            }

            dup2(outWrite.Get(), STDOUT_FILENO);
            dup2(errWrite.Get(), STDERR_FILENO);
            dup2(inRead.Get(), STDIN_FILENO);

            outRead.Reset();
            outWrite.Reset();
            errRead.Reset();
            errWrite.Reset();
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

        m_impl->childPid = static_cast<int>(pid);
        m_impl->stdoutRead.Reset(outRead.Release());
        m_impl->stderrRead.Reset(errRead.Release());
        m_impl->stdinWrite.Reset(inWrite.Release());

        outWrite.Reset();
        errWrite.Reset();
        inRead.Reset();

        m_lastExitCode = -1;
        m_activeReadLoops = 2;
        m_running = true;
        m_outputThread = std::thread(&ProcessBridge::ReadOutputLoop, this, std::move(onOutput));
        m_errorThread = std::thread(&ProcessBridge::ReadErrorLoop, this, std::move(onError));
        return "Process started.";
    }

    void ProcessBridge::ReadOutputLoop(OutputCallback onOutput) {
        std::array<char, 4096> buffer{};
        while (m_running && m_impl->stdoutRead.IsValid()) {
            const ssize_t bytesRead = read(m_impl->stdoutRead.Get(), buffer.data(), buffer.size() - 1);
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
        if (m_activeReadLoops.fetch_sub(1) == 1) {
            m_running = false;
        }
    }

    void ProcessBridge::ReadErrorLoop(ErrorCallback onError) {
        std::array<char, 4096> buffer{};
        while (m_running && m_impl->stderrRead.IsValid()) {
            const ssize_t bytesRead = read(m_impl->stderrRead.Get(), buffer.data(), buffer.size() - 1);
            if (bytesRead > 0) {
                buffer[static_cast<size_t>(bytesRead)] = '\0';
                onError(std::string(buffer.data(), static_cast<size_t>(bytesRead)));
                continue;
            }

            if (bytesRead < 0 && errno == EINTR) {
                continue;
            }

            break;
        }
        if (m_activeReadLoops.fetch_sub(1) == 1) {
            m_running = false;
        }
    }

    void ProcessBridge::SendInput(const std::string& input) {
        if (!m_running || !m_impl->stdinWrite.IsValid()) return;

        const std::string payload = input + "\n";
        const char* data = payload.data();
        size_t remaining = payload.size();

        while (remaining > 0) {
            const ssize_t written = write(m_impl->stdinWrite.Get(), data, remaining);
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

        m_impl->stdinWrite.Reset();
        m_impl->stdoutRead.Reset();
        m_impl->stderrRead.Reset();

        if (m_impl->childPid > 0) {
            kill(m_impl->childPid, SIGTERM);
            (void)waitpid(m_impl->childPid, nullptr, 0);
            m_lastExitCode = 0;
            m_impl->childPid = -1;
        }

        JoinIoThreads();
    }
#endif

    int ProcessBridge::WaitForExit() {
#ifdef _WIN32
        if (!m_impl->childProcess.IsValid()) {
            return m_lastExitCode.load();
        }

        (void)WaitForSingleObject(m_impl->childProcess.Get(), INFINITE);
        DWORD exitCode = 0;
        if (!GetExitCodeProcess(m_impl->childProcess.Get(), &exitCode)) {
            m_lastExitCode = -1;
        } else {
            m_lastExitCode = static_cast<int>(exitCode);
        }

        m_running = false;
        m_impl->childProcess.Reset();
        JoinIoThreads();
        return m_lastExitCode.load();
#else
        if (m_impl->childPid <= 0) {
            return m_lastExitCode.load();
        }

        int status = 0;
        pid_t result = -1;
        do {
            result = waitpid(m_impl->childPid, &status, 0);
        } while (result == -1 && errno == EINTR);

        if (result == -1) {
            m_lastExitCode = -1;
        } else if (WIFEXITED(status)) {
            m_lastExitCode = WEXITSTATUS(status);
        } else if (WIFSIGNALED(status)) {
            m_lastExitCode = 128 + WTERMSIG(status);
        } else {
            m_lastExitCode = -1;
        }

        m_running = false;
        m_impl->childPid = -1;
        JoinIoThreads();
        return m_lastExitCode.load();
#endif
    }

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
ProcessBridge usa una PIMPL per isolare completamente i dettagli di piattaforma dall'header pubblico.
Il file header non include più windows.h né tipi POSIX, quindi i consumer restano portabili.
Nel source vengono usati i wrapper RAII condivisi di ZeriLink (ScopedHandle/ScopedFd) per gestire risorse native.
La logica Run/SendInput/Terminate mantiene il comportamento esistente ma centralizza lo stato OS in Impl.
La gestione I/O asincrona ora separa stdout e stderr con pipe e thread dedicati, inoltrando i flussi su callback distinti.
*/
