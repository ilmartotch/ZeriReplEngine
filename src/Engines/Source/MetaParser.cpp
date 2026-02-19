#include "../Include/MetaParser.h"
#include <sstream>
#include <array>
#include <memory_resource>
#include <string_view>

namespace Zeri::Engines::Defaults {

    std::expected<Command, ParseError> MetaParser::Parse(const std::string& input) {
        if (input.empty()) {
            return Command{ .type = InputType::Empty };
        }

        Command cmd;
        cmd.rawInput = input;

        // Classification
        std::string_view inputView{ input };
        char prefix = inputView.front();
        if (prefix == '/') {
            cmd.type = InputType::Command;
        } else if (prefix == '$') {
            cmd.type = InputType::ContextSwitch;
        } else if (prefix == '!') {
            cmd.type = InputType::SystemOp;
        } else {
            return std::unexpected(ParseError{ "Invalid syntax. Start with '/' for commands or '$' for context.", 0 });
        }

        // Tokenization (Handling quotes), skip the prefix char for the first token
        std::array<std::byte, 4096> scratch{};
        std::pmr::monotonic_buffer_resource tempResource{ scratch.data(), scratch.size() };

        auto cleanInput = inputView.substr(1);
        auto tokens = Tokenize(cleanInput, &tempResource);

        if (tokens.empty()) {
             return std::unexpected(ParseError{ "No command specified.", 1 });
        }

        cmd.commandName.assign(tokens[0].begin(), tokens[0].end());

        // Argument Parsing
        for (size_t i = 1; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            if (token.starts_with("--")) {
                cmd.flags.emplace(std::string(token.begin() + 2, token.end()), "true");
            } else {
                cmd.args.emplace_back(token.begin(), token.end());
            }
        }

        return cmd;
    }

    std::pmr::vector<std::pmr::string> MetaParser::Tokenize(std::string_view input, std::pmr::memory_resource* memory) {
        std::pmr::vector<std::pmr::string> tokens{ memory };
        std::pmr::string currentToken{ memory };
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
                    tokens.emplace_back(currentToken);
                    currentToken.clear();
                }
            } else {
                currentToken += c;
            }
        }

        if (!currentToken.empty()) {
            tokens.emplace_back(currentToken);
        }

        return tokens;
    }

}

/*
The Tokenize method uses a boolean state `inQuotes` to decide whether a space character
should split the token or be included in it.
It also supports basic escaping (`\"`) allowing quotes inside quoted strings.
The Parse method acts as the "Classifier", mapping the raw string to the specific Zeri semantics ($ vs /).
*/
