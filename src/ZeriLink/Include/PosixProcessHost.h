#pragma once

#include "IProcessHost.h"
#include "ScopedHandle.h"
#include <mutex>
#include <sys/types.h>

namespace Zeri::Link {

    class PosixProcessHost final : public IProcessHost {
    public:
        PosixProcessHost() = default;
        ~PosixProcessHost() override;

        [[nodiscard]] bool Start(
            const std::string& executable,
            const std::vector<std::string>& args
        ) override;

        void Stop() override;

        [[nodiscard]] bool SendData(std::span<const std::byte> buffer) override;
        [[nodiscard]] bool ReceiveData(std::span<std::byte> buffer, size_t& bytesRead) override;
        [[nodiscard]] bool IsRunning() const override;

    private:
        pid_t m_childPid{ -1 };
        ScopedFd m_stdinWrite;
        ScopedFd m_stdoutRead;
        std::mutex m_writeMutex;
    };

}

/*
Manages a child process using fork/execvp with bidirectional pipes.
All file descriptors are wrapped in ScopedFd for deterministic RAII cleanup.

On Linux, PR_SET_PDEATHSIG ensures the child receives SIGTERM when the parent dies,
preventing orphaned sidecar processes. This is the POSIX equivalent of the Win32
Job Object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE used in WindowsProcessHost.

SendData is internally serialized via m_writeMutex as defense-in-depth; the
ProcessBridge layer also holds its own mutex on the write path.
ReceiveData has no mutex: it is called exclusively from the reader thread.
*/
