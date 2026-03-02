#pragma once

#include <string>
#include <vector>
#include <functional>
#include "../../Engines/Include/ExecutionResult.h"

namespace Zeri::Engines {

    class IProcessBridge {
    public:
        virtual ~IProcessBridge() = default;

        using OutputCallback = std::function<void(const std::string&)>;

        [[nodiscard]] virtual ExecutionOutcome Run(
            const std::string& executablePath,
            const std::vector<std::string>& args,
            OutputCallback onOutput
        ) = 0;

        [[nodiscard]] virtual int ExecuteSync(
            const std::string& executablePath,
            const std::vector<std::string>& args
        ) = 0;

        virtual void SendInput(const std::string& input) = 0;
        virtual void Terminate() = 0;
        [[nodiscard]] virtual bool IsRunning() const = 0;
    };

}

/*
@brief Interface for supervising external processes.
Captures output and redirects input in a thread-safe, cross-platform way.

IProcessBridge defines the contract for process supervision.
The OutputCallback allows the REPL to receive data from the child process asynchronously,
which is essential for the multi-threaded "Bridge" architecture.
*/
