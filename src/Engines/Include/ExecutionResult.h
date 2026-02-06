#pragma once
#include <string>
#include <expected>
#include <optional>
#include <vector>

namespace Zeri::Engines {

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

    using ExecutionOutcome = std::expected<std::string, ExecutionError>;

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