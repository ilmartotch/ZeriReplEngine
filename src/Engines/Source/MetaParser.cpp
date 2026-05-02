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

    [[nodiscard]] std::string ToLower(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return result;
    }

    [[nodiscard]] std::optional<size_t> FindUnquotedPipePosition(std::string_view input) {
        bool inQuotes = false;
        bool escape = false;

        for (size_t i = 0; i < input.size(); ++i) {
            const char c = input[i];

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
                continue;
            }

            if (c == '|' && !inQuotes) {
                return i;
            }
        }

        return std::nullopt;
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
    [[nodiscard]] std::string_view StripComment(std::string_view input) {
        bool inQuotes = false;
        bool escape = false;
        for (size_t i = 0; i < input.size(); ++i) {
            char c = input[i];
            if (escape) { escape = false; continue; }
            if (c == '\\') { escape = true; continue; }
            if (c == '"') { inQuotes = !inQuotes; continue; }
            if (c == '#' && !inQuotes) {
                auto stripped = input.substr(0, i);
                while (!stripped.empty() && std::isspace(static_cast<unsigned char>(stripped.back()))) {
                    stripped.remove_suffix(1);
                }
                return stripped;
            }
        }
        return input;
    }
}

namespace Zeri::Engines::Defaults {

    std::expected<Command, ParseError> MetaParser::Parse(const std::string& input) {
        std::string_view inputView{ input };
        inputView = Trim(inputView);

        if (inputView.empty()) {
            return Command{ .type = InputType::Empty };
        }

        inputView = StripComment(inputView);
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
            cmd.type = InputType::Expression;
            cmd.commandName = "@expr";
            cmd.args.emplace_back(std::string(inputView));
            return cmd;
        }

        if (const auto pipePos = FindUnquotedPipePosition(inputView); pipePos.has_value()) {
            return std::unexpected(ParseError{
                "Pipe operator '|' is not supported in this parser.",
                *pipePos
            });
        }

        std::array<std::byte, 4096> scratch{};
        std::pmr::monotonic_buffer_resource tempResource{ scratch.data(), scratch.size() };

        auto cleanInput = inputView.substr(1);
        auto tokens = Tokenize(cleanInput, &tempResource);

        if (tokens.empty()) {
            return std::unexpected(ParseError{ "Missing command or context name after prefix.",1 });
        }

        cmd.commandName = ToLower(std::string_view(tokens[0].data(), tokens[0].size()));

        std::string lastFlag;
        for (size_t i = 1; i < tokens.size(); ++i) {
            const auto& token = tokens[i];
            if (token.starts_with("--")) {
                std::string flagName(token.begin() + 2, token.end());
                lastFlag = flagName;
                cmd.flags[flagName] = "true";
            } else {
                if (!lastFlag.empty()) {
                    cmd.flags[lastFlag] = std::string(token.begin(), token.end());
                    lastFlag.clear();
                } else {
                    cmd.args.emplace_back(token.begin(), token.end());
                }
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
MetaParser.cpp — Primary parser for the meta-language.

  - Trim: removes leading/trailing whitespace from input.
  - StripComment: strips everything from the first unquoted # character to
    the end of the line. Respects quoted strings so '#' inside quotes is
    preserved. Satisfies the meta-language spec symbol semantics for #.
  - FindUnclosedQuotePosition: detects unclosed quotes and returns the
    position for caret-based error reporting.
  - ToLower: case-folds a string_view to lowercase. Used to normalize
    commandName after tokenization, satisfying the spec requirement that
    commands and keywords are case-insensitive.
  - Parse: Trim -> StripComment -> FindUnclosedQuotePosition -> prefix
    detection (/ $ !) -> Tokenize -> ToLower(commandName) -> flag/arg split,
    with support for '--flag value' (value binds to the most recent flag,
    defaulting to 'true' when no value follows).
  - Tokenize: PMR-backed tokenizer with quote handling, escape sequences,
    and @ variable prefix support.
*/
