#pragma once

#include <string>
#include <vector>
#include <functional>
#include <filesystem>
#include <optional>
#include "../../Engines/Include/ExecutionResult.h"

namespace Zeri::Engines {

    class IProcessBridge {
    public:
        virtual ~IProcessBridge() = default;

        using OutputCallback = std::function<void(const std::string&)>;

        [[nodiscard]] virtual ExecutionOutcome Run(
            const std::filesystem::path& executablePath,
            const std::vector<std::string>& args,
            OutputCallback onOutput,
            const std::optional<std::filesystem::path>& cwd = std::nullopt
        ) = 0;

        [[nodiscard]] virtual int ExecuteSync(
            const std::filesystem::path& executablePath,
            const std::vector<std::string>& args,
            const std::optional<std::filesystem::path>& cwd = std::nullopt
        ) = 0;

        virtual void SendInput(const std::string& input) = 0;
        virtual void Terminate() = 0;
        [[nodiscard]] virtual bool IsRunning() const = 0;
    };

}

/*
Interface for supervising external processes.
Captures output and redirects input in a thread-safe, cross-platform way.

IProcessBridge defines the contract for process supervision.
- Run(): Asynchronous execution with output streaming via callback.
- ExecuteSync(): Blocking execution returning exit code.
- Both accept std::filesystem::path for type-safe, Unicode-aware executable paths,
  and an optional cwd parameter to set the child's working directory.

Run:
Launch a child process asynchronously with piped I/O.
executablePath: Filesystem path to the executable (UTF-8 safe).
args: Command-line arguments forwarded to the process.
onOutput: Callback invoked on every chunk of stdout/stderr.
cwd: Optional working directory for the child process.

ExecuteSync:
Launch a child process synchronously and return its exit code.
executablePath: Filesystem path to the executable.
args: Command-line arguments forwarded to the process.
cwd: Optional working directory for the child process.
*/
