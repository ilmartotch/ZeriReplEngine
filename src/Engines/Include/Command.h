#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>

namespace Zeri::Engines {

    enum class InputType {
        Command,
        ContextSwitch,
        SystemOp,
        Empty,
        Unknown
    };

    enum class CommandPrefix {
        None,
        Slash,
        Bang
    };

    struct FunctionCall {
        std::string name;
        std::vector<std::string> positionalArgs;
        std::map<std::string, std::string> namedArgs;
    };

    struct Command {
        InputType type = InputType::Unknown;
        std::string rawInput;
        std::string commandName;
        std::vector<std::string> args;
        std::map<std::string, std::string> flags; // --verbose -> "true"
        std::optional<std::pair<std::string, std::string>> assignment;
        std::optional<FunctionCall> functionCall;

        [[nodiscard]] bool empty() const {
            return type == InputType::Empty || commandName.empty();
        }

        [[nodiscard]] bool isContextCommand() const {
            return type == InputType::Command;
        }
    };

}