#pragma once
#include <string>
#include <expected>
#include <optional>
#include <vector>

namespace Zeri::Engines {

    enum class ExecutionMessageKind {
        Output,
        Info,
        Success,
        Warning
    };

    struct ExecutionMessage {
        ExecutionMessageKind kind{ ExecutionMessageKind::Output };
        std::string text;

        ExecutionMessage() = default;
        ExecutionMessage(const char* value) : text(value == nullptr ? "" : value) {}
        ExecutionMessage(std::string value) : text(std::move(value)) {}
        ExecutionMessage(ExecutionMessageKind messageKind, std::string value)
            : kind(messageKind), text(std::move(value)) {}

        [[nodiscard]] bool empty() const { return text.empty(); }
    };

    struct ExecutionError {
        std::string code;
        std::string message;
        std::optional<std::string> context;
        std::vector<std::string> hints;

        [[nodiscard]] std::string Format() const {
            std::string result = "[" + code + "] " + message;
            if (context.has_value()) {
                result += " (at: " + context.value() + ")";
            }
            for (const auto& hint : hints) {
                result += "\n  Hint: " + hint;
            }
            return result;
        }
    };

    [[nodiscard]] inline ExecutionMessage Info(std::string value) {
        return ExecutionMessage{ ExecutionMessageKind::Info, std::move(value) };
    }

    [[nodiscard]] inline ExecutionMessage Success(std::string value) {
        return ExecutionMessage{ ExecutionMessageKind::Success, std::move(value) };
    }

    [[nodiscard]] inline ExecutionMessage Warning(std::string value) {
        return ExecutionMessage{ ExecutionMessageKind::Warning, std::move(value) };
    }

    using ExecutionOutcome = std::expected<ExecutionMessage, ExecutionError>;

}

/*
Defines standard types for execution results using C++23 `std::expected`.
Extended to support meta-language error requirements: "Error Handling"

"Each error includes a clear message, contextual information, and optional correction hints."

- `code`: typed error identifier
- `message`: human-readable description
- `context`: where the error occurred
- `hints`: actionable suggestions for correction
*/