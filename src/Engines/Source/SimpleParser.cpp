#include "../Include/SimpleParser.h"
#include <sstream>

namespace Zeri::Engines::Defaults {

    std::expected<Command, ParseError> SimpleParser::Parse(const std::string& input) {
        if (input.empty()) {
            return Command{};
        }

        Command cmd;
        cmd.rawInput = input;

        std::istringstream iss(input);
        iss >> cmd.commandName;

        std::string arg;
        while (iss >> arg) {
            if (arg.starts_with("--")) {
                cmd.flags[arg.substr(2)] = "true";
            } else {
                cmd.args.push_back(arg);
            }
        }

        return cmd;
    }

}

/*
Implementation of `SimpleParser`.
Uses `std::stringstream` for whitespace tokenization.
This logic is simplistic and does not handle quoted strings or complex command structures,
which is acceptable for v0.1.
*/
