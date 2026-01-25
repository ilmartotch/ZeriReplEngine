#pragma once
#include <string>
#include <expected>

namespace Zeri::Engines {

    struct ExecutionError {
        std::string code;
        std::string message;
    };

    using ExecutionOutcome = std::expected<std::string, ExecutionError>;

}

/*
Defines standard types for execution results using C++23 `std::expected`.
This enforces error handling by typing the return value of an execution as either a success (std::string output)
or a failure (ExecutionError). This avoids the use of exceptions for control flow in normal
command execution scenarios.
*/
