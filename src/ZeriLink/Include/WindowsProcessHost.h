#pragma once

#include "IProcessHost.h"
#include "ScopedHandle.h"
#include <mutex>

namespace Zeri::Link {

    class WindowsProcessHost final : public IProcessHost {
    public:
        WindowsProcessHost() = default;
        ~WindowsProcessHost() override;

        [[nodiscard]] bool Start(
            const std::string& executable,
            const std::vector<std::string>& args
        ) override;

        void Stop() override;

        [[nodiscard]] bool SendData(std::span<const std::byte> buffer) override;
        [[nodiscard]] bool ReceiveData(std::span<std::byte> buffer, size_t& bytesRead) override;
        [[nodiscard]] bool IsRunning() const override;

    private:
        ScopedHandle m_process;
        ScopedHandle m_job;
        ScopedHandle m_stdinWrite;
        ScopedHandle m_stdoutRead;
        std::mutex m_writeMutex;
    };

}

/*
Manages a child process using CreateProcessW with bidirectional Anonymous Pipes.
All OS handles (process, job object, pipe endpoints) are wrapped in ScopedHandle
for deterministic RAII cleanup.

The Job Object with JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE guarantees that the child
process tree is terminated when the host is destroyed, even if the core crashes.
This is critical for preventing orphaned Python/Bun processes.

SendData is internally serialized via m_writeMutex as defense-in-depth; the
ProcessBridge layer also holds its own mutex on the write path.
ReceiveData has no mutex: it is called exclusively from the reader thread.
*/
