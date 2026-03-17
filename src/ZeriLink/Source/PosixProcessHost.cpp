#include "../Include/PosixProcessHost.h"

#ifndef _WIN32

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

#ifdef __linux__
    #include <sys/prctl.h>
#endif

namespace {

    bool CreatePipePair(int pipefd[2]) {
#ifdef __linux__
        return pipe2(pipefd, O_CLOEXEC) == 0;
#else
        if (pipe(pipefd) != 0) return false;
        fcntl(pipefd[0], F_SETFD, FD_CLOEXEC);
        fcntl(pipefd[1], F_SETFD, FD_CLOEXEC);
        return true;
#endif
    }

}

namespace Zeri::Link {

    PosixProcessHost::~PosixProcessHost() {
        Stop();
    }

    bool PosixProcessHost::Start(
        const std::string& executable,
        const std::vector<std::string>& args
    ) {
        if (m_childPid > 0) return false;

        int outPipe[2]{ -1, -1 };
        int inPipe[2]{ -1, -1 };

        if (!CreatePipePair(outPipe)) return false;
        ScopedFd outRead(outPipe[0]);
        ScopedFd outWrite(outPipe[1]);

        if (!CreatePipePair(inPipe)) return false;
        ScopedFd inRead(inPipe[0]);
        ScopedFd inWrite(inPipe[1]);

        const pid_t pid = fork();
        if (pid < 0) return false;

        if (pid == 0) {
#ifdef __linux__
            prctl(PR_SET_PDEATHSIG, SIGTERM);
            if (getppid() == 1) _exit(1);
#endif
            dup2(outWrite.Get(), STDOUT_FILENO);
            dup2(outWrite.Get(), STDERR_FILENO);
            dup2(inRead.Get(), STDIN_FILENO);

            outRead.Reset();
            outWrite.Reset();
            inRead.Reset();
            inWrite.Reset();

            std::vector<char*> argv;
            argv.reserve(args.size() + 2);
            argv.push_back(const_cast<char*>(executable.c_str()));
            for (const auto& a : args) {
                argv.push_back(const_cast<char*>(a.c_str()));
            }
            argv.push_back(nullptr);

            execvp(executable.c_str(), argv.data());
            _exit(127);
        }

        m_childPid = pid;
        m_stdoutRead = std::move(outRead);
        m_stdinWrite = std::move(inWrite);

        return true;
    }

    void PosixProcessHost::Stop() {
        m_stdinWrite.Reset();
        m_stdoutRead.Reset();

        if (m_childPid > 0) {
            kill(m_childPid, SIGTERM);

            int status = 0;
            pid_t result = waitpid(m_childPid, &status, WNOHANG);

            if (result == 0) {
                struct timespec ts{ 0, 100'000'000 };
                nanosleep(&ts, nullptr);
                result = waitpid(m_childPid, &status, WNOHANG);
            }

            if (result == 0) {
                kill(m_childPid, SIGKILL);
                waitpid(m_childPid, nullptr, 0);
            }

            m_childPid = -1;
        }
    }

    bool PosixProcessHost::SendData(std::span<const std::byte> buffer) {
        std::lock_guard lock(m_writeMutex);

        if (!m_stdinWrite.IsValid()) return false;

        const auto* data = reinterpret_cast<const char*>(buffer.data());
        size_t remaining = buffer.size();

        while (remaining > 0) {
            const ssize_t written = write(m_stdinWrite.Get(), data, remaining);
            if (written > 0) {
                data += written;
                remaining -= static_cast<size_t>(written);
                continue;
            }
            if (written < 0 && errno == EINTR) continue;
            return false;
        }

        return true;
    }

    bool PosixProcessHost::ReceiveData(std::span<std::byte> buffer, size_t& bytesRead) {
        bytesRead = 0;
        if (!m_stdoutRead.IsValid()) return false;

        while (true) {
            const ssize_t result = read(
                m_stdoutRead.Get(),
                buffer.data(),
                buffer.size()
            );

            if (result > 0) {
                bytesRead = static_cast<size_t>(result);
                return true;
            }

            if (result < 0 && errno == EINTR) continue;
            return false;
        }
    }

    bool PosixProcessHost::IsRunning() const {
        if (m_childPid <= 0) return false;
        const int result = waitpid(m_childPid, nullptr, WNOHANG);
        return result == 0;
    }

    std::unique_ptr<IProcessHost> IProcessHost::CreateProcessHost() {
        return std::make_unique<PosixProcessHost>();
    }

}

#endif

/*
Start() sequence:
1. Creates two pipe pairs using pipe2(O_CLOEXEC) on Linux, or pipe()+fcntl on
   other POSIX systems. O_CLOEXEC ensures file descriptors are automatically closed
   in child processes spawned by exec*, preventing fd leaks to grandchildren.
2. fork() splits the process. In the child:
   a. PR_SET_PDEATHSIG (Linux only) sets the child to receive SIGTERM when the
      parent dies. The getppid()==1 check handles the race where the parent already
      exited before prctl was called (PID 1 = init/systemd adopted the orphan).
   b. dup2() redirects stdout, stderr, and stdin to the pipe endpoints.
   c. All four ScopedFd objects are Reset(), closing the original pipe fds in the child
      (the dup2'd copies on 0/1/2 remain open).
   d. execvp() replaces the child image. On failure, _exit(127) avoids running
      destructors in the forked copy of the parent's address space.
3. In the parent: ownership of outRead and inWrite transfers to member ScopedFds.
   outWrite and inRead are destroyed, closing the child-side endpoints in the parent.

Stop() escalation sequence:
1. Closes pipes first to unblock any pending read() in the reader thread.
2. Sends SIGTERM for graceful shutdown.
3. Checks with waitpid(WNOHANG) if the child exited immediately.
4. If still alive, waits 100ms then checks again.
5. If still alive after the grace period, sends SIGKILL (unblockable) and does a
   blocking waitpid to reap the zombie. This prevents zombie process accumulation.

SendData() write loop handles partial writes and EINTR (signal interruption during
write). On POSIX, write() on a pipe can return less than requested or be interrupted
by a signal handler — both are normal conditions that must be retried.

ReceiveData() retry loop handles EINTR transparently. Returns false on EOF (read
returns 0, meaning the child closed its stdout) or on unrecoverable error.

IsRunning() uses waitpid(WNOHANG): returns 0 if the child is still running,
positive pid if it exited (reaps zombie), or -1 on error.
Note: this has a side effect of reaping the zombie if the child already exited.

The factory method CreateProcessHost() is defined here under #ifndef _WIN32.
On Windows builds this entire file is excluded; the factory lives in
WindowsProcessHost.cpp instead.
*/
