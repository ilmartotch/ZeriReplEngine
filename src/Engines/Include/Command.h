#pragma once
#include <string>
#include <vector>
#include <map>

namespace Zeri::Engines {

    struct Command {
        std::string rawInput;
        std::string commandName;
        std::vector<std::string> args;
        std::map<std::string, std::string> flags;

        [[nodiscard]] bool empty() const {
            return commandName.empty();
        }
    };

}

/*
Defines the `Command` structure, which acts as the Data Transfer Object (DTO) between the Parser
and the rest of the system (Dispatcher, Executors).
It decouples the raw string input from its semantic meaning, allowing different parsers to produce
a standard command format that the Dispatcher can route without knowing the original syntax.
*/
