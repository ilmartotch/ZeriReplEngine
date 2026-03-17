#pragma once

#include <cstddef>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace Zeri::Link {

    class IProcessHost {
    public:
        virtual ~IProcessHost() = default;

        IProcessHost() = default;
        IProcessHost(const IProcessHost&) = delete;
        IProcessHost& operator=(const IProcessHost&) = delete;
        IProcessHost(IProcessHost&&) = delete;
        IProcessHost& operator=(IProcessHost&&) = delete;

        [[nodiscard]] virtual bool Start(
            const std::string& executable,
            const std::vector<std::string>& args
        ) = 0;

        virtual void Stop() = 0;

        [[nodiscard]] virtual bool SendData(std::span<const std::byte> buffer) = 0;
        [[nodiscard]] virtual bool ReceiveData(std::span<std::byte> buffer, size_t& bytesRead) = 0;
        [[nodiscard]] virtual bool IsRunning() const = 0;

        [[nodiscard]] static std::unique_ptr<IProcessHost> CreateProcessHost();
    };

}

/*

Pure virtual interface that isolates all platform-specific subprocess management
behind a single contract. The ProcessBridge core depends only on IProcessHost,
never on Win32 or POSIX headers directly. Zero #ifdef in business logic.

Implementations:
- WindowsProcessHost: CreateProcessW, Job Objects (KILL_ON_JOB_CLOSE), Anonymous Pipes
- PosixProcessHost: fork/execvp, pipe2, waitpid, PR_SET_PDEATHSIG

Methods:
- Start(executable, args): Spawns child process with bidirectional pipe pairs.
- Stop(): Terminates process and releases OS resources. Idempotent.
- SendData(span<const byte>): Writes raw bytes to child stdin. Not internally synchronized;
  the ProcessBridge layer serializes writes via its own mutex.
- ReceiveData(span<byte>, bytesRead): Blocking read from child stdout.
  Partial reads expected; the FrameDecoder accumulates bytes incrementally.
- IsRunning(): Non-blocking poll (WaitForSingleObject timeout 0 / waitpid WNOHANG).

Factory:
- CreateProcessHost(): Compile-time dispatch via #ifdef _WIN32.
  Declared here, defined in the platform-specific .cpp to keep this header OS-header-free.

Design:
- std::span<const std::byte> for I/O: type-safe, zero-copy, no void* casts.
- Copy/move deleted: IProcessHost owns OS resources (HANDLE/fd) that must not be duplicated.
- Thread safety delegated to ProcessBridge (single mutex on write path).
*/
