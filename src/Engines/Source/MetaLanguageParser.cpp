#include "../Include/MetaLanguageParser.h"
#include <sstream>
#include <algorithm>

namespace Zeri::Engines::Defaults {

    std::expected<Command, ParseError> MetaLanguageParser::Parse(const std::string& input) {
        if (input.empty()) return Command{};

        Command cmd;
        cmd.rawInput = input;

        if (input.starts_with('/')) {
            std::string cleanInput = input.substr(1);
            std::istringstream iss(cleanInput);
            iss >> cmd.commandName;

            std::string arg;
            while (iss >> arg) {
                if (arg.starts_with("--")) {
                    cmd.flags[arg.substr(2)] = "true";
                } else {
                    cmd.args.push_back(arg);
                }
            }
        } else {
            cmd.commandName = "@context_eval";
            cmd.args.push_back(input);
        }

        return cmd;
    }

    std::string MetaLanguageParser::StripComment(const std::string& input, std::string& outComment) const {
        auto pos = input.find('#');
        if (pos == std::string::npos) {
            return input;
        }
        outComment = input.substr(pos + 1);
        std::string result = input.substr(0, pos);
        while (!result.empty() && result.back() == ' ') {
            result.pop_back();
        }
        return result;
    }

    bool MetaLanguageParser::TryParseAssignment(const std::string& input, Command& cmd) const {
        auto pos = input.find('=');
        if (pos == std::string::npos || pos == 0) {
            return false;
        }

        std::string varName = input.substr(0, pos);
        std::string value = (pos + 1 < input.size()) ? input.substr(pos + 1) : "";

        while (!varName.empty() && varName.back() == ' ') varName.pop_back();
        while (!value.empty() && value.front() == ' ') value.erase(0, 1);

        if (varName.empty()) {
            return false;
        }

        cmd.assignment = { varName, value };
        return true;
    }

    bool MetaLanguageParser::TryParseFunctionCall(const std::string& input, Command& cmd) const {
        auto parenOpen = input.find('(');
        auto parenClose = input.rfind(')');

        if (parenOpen == std::string::npos || parenClose == std::string::npos || parenClose < parenOpen) {
            return false;
        }

        FunctionCall fc;
        fc.name = input.substr(0, parenOpen);
        while (!fc.name.empty() && fc.name.back() == ' ') fc.name.pop_back();
        std::string argsStr = input.substr(parenOpen + 1, parenClose - parenOpen - 1);
        if (!argsStr.empty()) {
            fc.positionalArgs.push_back(argsStr);
        }

        cmd.functionCall = fc;
        return true;
    }

    CommandPrefix MetaLanguageParser::DetectPrefix(const std::string& input) const {
        if (input.empty()) return CommandPrefix::None;
        if (input[0] == '/') return CommandPrefix::Slash;
        if (input[0] == '!') return CommandPrefix::Bang;
        return CommandPrefix::None;
    }

}

/*
Implementation of `MetaLanguageParser`.
Parses input according to meta-language symbol semantics:
- # comments are stripped and stored (spec: "Symbol Semantics")
- = triggers variable assignment detection (spec: "Variables and Scope")
- () triggers function call detection (spec: "Function Invocation")
- / and ! detect command prefixes (spec: "Command Model")
*/