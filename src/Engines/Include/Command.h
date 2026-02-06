#pragma once

#include <string>
#include <vector>
#include <map>

namespace Zeri::Engines {

    enum class InputType {
        Command,        // Starts with /
        ContextSwitch,  // Starts with $
        SystemOp,       // Starts with ! (future use)
        Empty,
        Unknown
    };

    struct Command {
        InputType type = InputType::Unknown;
        std::string rawInput;
        std::string target; // The command name OR the context name
        std::vector<std::string> args;
        std::map<std::string, std::string> flags; // --verbose -> "true"

        [[nodiscard]] bool empty() const {
            return type == InputType::Empty || target.empty();
        }

        [[nodiscard]] bool isContextCommand() const {
            return type == InputType::Command;
        }
    };

}