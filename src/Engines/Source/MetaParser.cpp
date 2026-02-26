#include "../Include/MetaParser.h"
#include <sstream>
#include <array>
#include <memory_resource>
#include <string_view>
#include <cctype>

namespace {
    [[nodiscard]] std::string_view Trim(std::string_view value) {
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return value;
    }

    [[nodiscard]] std::optional<size_t> FindUnclosedQuotePosition(std::string_view input) {
        bool inQuotes = false;
        bool escape = false;
        size_t lastQuotePos = 0;

        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];

            if (escape) {
                escape = false;
                continue;
            }

            if (c == '\\') {
                escape = true;
                continue;
            }

            if (c == '"') {
                inQuotes = !inQuotes;
                lastQuotePos = i;
            }
        }

        if (inQuotes) return lastQuotePos;
        return std::nullopt;
    }
}

namespace Zeri::Engines::Defaults {

    std::expected<Command, ParseError> MetaParser::Parse(const std::string& input) {
        std::string_view inputView{ input };
        inputView = Trim(inputView);

        if (inputView.empty()) {
            return Command{ .type = InputType::Empty };
        }

        if (auto unclosedQuotePos = FindUnclosedQuotePosition(inputView); unclosedQuotePos.has_value()) {
            return std::unexpected(ParseError{
                "Unclosed quoted string.",
                *unclosedQuotePos
            });
        }

        Command cmd;
        cmd.rawInput = std::string(inputView);

        char prefix = inputView.front();
        if (prefix == '/') {
            cmd.type = InputType::Command;
        } else if (prefix == '$') {
            cmd.type = InputType::ContextSwitch;
        } else if (prefix == '!') {
            cmd.type = InputType::SystemOp;
        } else {
            return std::unexpected(ParseError{
                "Invalid syntax. Input must start with '/', '$' or '!'.",
                0
            });
        }

        std::array<std::byte, 4096> scratch{};
        std::pmr::monotonic_buffer_resource tempResource{ scratch.data(), scratch.size() };

        auto cleanInput = inputView.substr(1);
        auto tokens = Tokenize(cleanInput, &tempResource);

        if (tokens.empty()) {
            return std::unexpected(ParseError{ "Missing command or context name after prefix.",1 });
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
        bool hasVariablePrefix = false;

        auto pushToken = [&]() {
            if (currentToken.empty() && !hasVariablePrefix) {
                return;
            }

            std::pmr::string token{ memory };
            if (hasVariablePrefix) {
                token += '@';
            }
            token += currentToken;

            tokens.emplace_back(std::move(token));
            currentToken.clear();
            hasVariablePrefix = false;
        };

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

            if (!inQuotes && currentToken.empty() && !hasVariablePrefix && c == '@') {
                hasVariablePrefix = true;
                continue;
            }

            if (c == '"') {
                inQuotes = !inQuotes;
                continue;
            }

            if (c == ' ' && !inQuotes) {
                pushToken();
            } else {
                currentToken += c;
            }
        }

        pushToken();
        return tokens;
    }

}

/*
 - Trim function: trims the input string_view in the Parse method to remove unwanted spaces.
 - FindUnclosedQuotePosition function: Added to detect unclosed quotes in the input.
 - Parse method: checks for unclosed quotes and trims the input. Improperly formatted inputs are rejected with an error.
 - Tokenization and command parsing logic remains the same.
 */
