#include "../Include/MetaParser.h"
#include <sstream>

namespace Zeri::Engines::Defaults {

    std::expected<Command, ParseError> MetaParser::Parse(const std::string& input) {
        if (input.empty()) {
            return Command{ .type = InputType::Empty };
        }

        Command cmd;
        cmd.rawInput = input;

        // 1. Classification
        char prefix = input[0];
        if (prefix == '/') {
            cmd.type = InputType::Command;
        } else if (prefix == '$') {
            cmd.type = InputType::ContextSwitch;
        } else if (prefix == '!') {
            cmd.type = InputType::SystemOp;
        } else {
            // Defaulting to command if no prefix, or handle as error?
            // For Zeri v0.3, we enforce prefixes strictly.
            return std::unexpected(ParseError{ "Invalid syntax. Start with '/' for commands or '$' for context.", 0 });
        }

        // 2. Tokenization (Handling quotes)
        // We skip the prefix char for the first token
        std::string cleanInput = input.substr(1);
        auto tokens = Tokenize(cleanInput);

        if (tokens.empty()) {
             return std::unexpected(ParseError{ "No command specified.", 1 });
        }

        cmd.target = tokens[0];

        // 3. Argument Parsing
        for (size_t i = 1; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            if (token.starts_with("--")) {
                // Flag detection
                std::string flagName = token.substr(2);
                cmd.flags[flagName] = "true";
            } else {
                cmd.args.push_back(token);
            }
        }

        return cmd;
    }

    std::vector<std::string> MetaParser::Tokenize(const std::string& input) {
        std::vector<std::string> tokens;
        std::string currentToken;
        bool inQuotes = false;
        bool escape = false;

        for (char c : input) {
            if (escape) {
                currentToken += c;
                escape = false;
                continue;
            }

            if (c == '\\') {
                escape = true;
                continue;
            }

            if (c == '"') {
                inQuotes = !inQuotes;
                continue;
            }

            if (c == ' ' && !inQuotes) {
                if (!currentToken.empty()) {
                    tokens.push_back(currentToken);
                    currentToken.clear();
                }
            } else {
                currentToken += c;
            }
        }

        if (!currentToken.empty()) {
            tokens.push_back(currentToken);
        }

        return tokens;
    }

} 

/*
FILE DOCUMENTATION:
MetaParser Implementation.
The Tokenize method uses a boolean state `inQuotes` to decide whether a space character
should split the token or be included in it.
It also supports basic escaping (`\"`) allowing quotes inside quoted strings.
The Parse method acts as the "Classifier", mapping the raw string to the specific Zeri semantics ($ vs /).
*/
