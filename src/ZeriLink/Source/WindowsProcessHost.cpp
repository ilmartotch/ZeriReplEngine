#include "../Include/WindowsProcessHost.h"

#ifdef _WIN32

#include <string_view>

namespace {

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
            }
            else if (ch == L'"') {
                out.append(backslashes * 2 + 1, L'\\');
                out.push_back(L'"');
                backslashes = 0;
            }
            else {
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

    [[nodiscard]] std::wstring BuildCommandLine(
        const std::wstring& widePath,
        const std::vector<std::string>& args
    ) {
        std::wstring cmdLine = QuoteArgument(widePath);
        for (const auto& arg : args) {
            std::wstring wideArg = Utf8ToWide(arg);
            if (wideArg.empty() && !arg.empty()) return {};
            cmdLine.push_back(L' ');
            cmdLine.append(QuoteArgument(wideArg));
        }
        return cmdLine;
    }

}

namespace Zeri::Link {

    WindowsProcessHost::~WindowsProcessHost() {
        Stop();
    }

    bool WindowsProcessHost::Start(
        const std::string& executable,
        const std::vector<std::string>& args
    ) {
        if (m_process.IsValid()) return false;

        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(SECURITY_ATTRIBUTES);
        sa.bInheritHandle = TRUE;
        sa.lpSecurityDescriptor = nullptr;

        HANDLE outReadRaw = nullptr;
        HANDLE outWriteRaw = nullptr;
        if (!CreatePipe(&outReadRaw, &outWriteRaw, &sa, 0)) return false;
        ScopedHandle outRead(outReadRaw);
        ScopedHandle outWrite(outWriteRaw);

        if (!SetHandleInformation(outRead.Get(), HANDLE_FLAG_INHERIT, 0)) return false;

        HANDLE inReadRaw = nullptr;
        HANDLE inWriteRaw = nullptr;
        if (!CreatePipe(&inReadRaw, &inWriteRaw, &sa, 0)) return false;
        ScopedHandle inRead(inReadRaw);
        ScopedHandle inWrite(inWriteRaw);

        if (!SetHandleInformation(inWrite.Get(), HANDLE_FLAG_INHERIT, 0)) return false;

        ScopedHandle job(CreateJobObjectW(nullptr, nullptr));
        if (!job.IsValid()) return false;

        JOBOBJECT_EXTENDED_LIMIT_INFORMATION jobInfo{};
        jobInfo.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (!SetInformationJobObject(job.Get(), JobObjectExtendedLimitInformation, &jobInfo, sizeof(jobInfo))) {
            return false;
        }

        const std::wstring widePath = Utf8ToWide(executable);
        if (widePath.empty() && !executable.empty()) return false;

        std::wstring cmdLine = BuildCommandLine(widePath, args);
        if (cmdLine.empty() && (!executable.empty() || !args.empty())) return false;

        STARTUPINFOW si{};
        si.cb = sizeof(STARTUPINFOW);
        si.hStdError = outWrite.Get();
        si.hStdOutput = outWrite.Get();
        si.hStdInput = inRead.Get();
        si.dwFlags |= STARTF_USESTDHANDLES;

        PROCESS_INFORMATION pi{};
        if (!CreateProcessW(
            nullptr,
            cmdLine.data(),
            nullptr,
            nullptr,
            TRUE,
            CREATE_SUSPENDED | CREATE_NO_WINDOW,
            nullptr,
            nullptr,
            &si,
            &pi
        )) {
            return false;
        }

        ScopedHandle process(pi.hProcess);
        ScopedHandle thread(pi.hThread);

        if (!AssignProcessToJobObject(job.Get(), process.Get())) {
            TerminateProcess(process.Get(), 1);
            return false;
        }

        ResumeThread(thread.Get());

        m_process = std::move(process);
        m_job = std::move(job);
        m_stdinWrite = std::move(inWrite);
        m_stdoutRead = std::move(outRead);

        return true;
    }

    void WindowsProcessHost::Stop() {
        m_stdinWrite.Reset();
        m_stdoutRead.Reset();

        if (m_process.IsValid()) {
            TerminateProcess(m_process.Get(), 0);
            WaitForSingleObject(m_process.Get(), 3000);
        }

        m_job.Reset();
        m_process.Reset();
    }

    bool WindowsProcessHost::SendData(std::span<const std::byte> buffer) {
        std::lock_guard lock(m_writeMutex);

        if (!m_stdinWrite.IsValid()) return false;

        const auto* data = reinterpret_cast<const char*>(buffer.data());
        size_t remaining = buffer.size();

        while (remaining > 0) {
            DWORD written = 0;
            const DWORD chunk = static_cast<DWORD>(
                remaining > 0xFFFFFFFFu ? 0xFFFFFFFFu : remaining
            );
            if (!WriteFile(m_stdinWrite.Get(), data, chunk, &written, nullptr)) return false;
            if (written == 0) return false;
            data += written;
            remaining -= written;
        }

        return true;
    }

    bool WindowsProcessHost::ReceiveData(std::span<std::byte> buffer, size_t& bytesRead) {
        bytesRead = 0;
        if (!m_stdoutRead.IsValid()) return false;

        DWORD read = 0;
        const DWORD toRead = static_cast<DWORD>(
            buffer.size() > 0xFFFFFFFFu ? 0xFFFFFFFFu : buffer.size()
        );
        if (!ReadFile(m_stdoutRead.Get(), buffer.data(), toRead, &read, nullptr)) return false;
        if (read == 0) return false;

        bytesRead = static_cast<size_t>(read);
        return true;
    }

    bool WindowsProcessHost::IsRunning() const {
        if (!m_process.IsValid()) return false;
        return WaitForSingleObject(m_process.Get(), 0) == WAIT_TIMEOUT;
    }

    std::unique_ptr<IProcessHost> IProcessHost::CreateProcessHost() {
        return std::make_unique<WindowsProcessHost>();
    }

}

#endif

/*

Start() sequence:
1. Creates two Anonymous Pipe pairs (stdin, stdout) with SECURITY_ATTRIBUTES bInheritHandle=TRUE.
2. Calls SetHandleInformation on parent-side endpoints to prevent them from being inherited
   by the child (outRead for stdout, inWrite for stdin).
3. Creates a Job Object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE. When the ScopedHandle for
   the job is destroyed, Windows kills the entire child process tree automatically.
4. Launches the child with CreateProcessW in CREATE_SUSPENDED state, assigns it to the job,
   then resumes. This eliminates the race where the child could spawn grandchildren before
   being assigned to the job.
5. Transfers pipe endpoint ownership to member ScopedHandles. All temporary ScopedHandles
   (outWrite, inRead, thread) are destroyed at scope exit, closing the child-side pipe ends
   in the parent — this is essential for correct EOF signaling.

Stop() closes pipes first (to unblock any pending ReadFile in the reader thread), then
terminates the process with a 3-second wait, and finally releases job and process handles.

SendData() uses a write loop to handle partial writes from WriteFile. The DWORD cast caps
each write at 4GB (theoretical; pipes have much smaller buffers). The mutex serializes
concurrent writes from different threads.

ReceiveData() is a single blocking ReadFile. Returns false on pipe closed (EOF) or error,
which signals the ProcessBridge reader thread to exit its loop.

IsRunning() uses WaitForSingleObject with timeout 0: returns WAIT_TIMEOUT if the process
is alive, WAIT_OBJECT_0 if it has exited.

The factory method CreateProcessHost() is defined here and returns a WindowsProcessHost.
On POSIX builds this entire file is excluded by the #ifdef _WIN32 guard; the factory
is defined in PosixProcessHost.cpp instead.

Helper functions (anonymous namespace):
- Utf8ToWide: Converts UTF-8 std::string_view to std::wstring via MultiByteToWideChar.
- QuoteArgument: Escapes a wide string for the Win32 command line, handling backslashes
  and quotes per the MSDN CreateProcess documentation.
- BuildCommandLine: Assembles the full command line from executable + args.
  Returns empty on UTF-8 conversion failure, allowing Start() to fail cleanly.
*/