#include "../Include/TerminalUi.h"
#include <iostream>
#include <print>
#include <string_view>
#include <cctype>

namespace Zeri::Ui {
    namespace {
        constexpr std::string_view kBlockDelimiterStart = "<<";
		constexpr std::string_view kBlockDelimiterEnd = ">>";
        constexpr std::string_view kContinuationPrompt = "| ";

        [[nodiscard]] std::string_view Trim(std::string_view value) {
            auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) {
                value.remove_prefix(1);
            }
            while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) {
                value.remove_suffix(1);
            }
            return value;
        }

        [[nodiscard]] std::optional<std::string> ReadBlockFromStdin() {
            std::string block;
            bool first = true;

            while (true) {
                std::print("\033[36m{}\033[0m", kContinuationPrompt);
                std::string line;
                if (!std::getline(std::cin, line)) {
                    return std::nullopt;
                }

                if (Trim(line) == kBlockDelimiterEnd) {
                    break;
                }

                if (!first) {
                    block.push_back('\n');
                }
                block += line;
                first = false;
            }

            return block;
        }
    }

    void TerminalUi::Write(const std::string& text) {
        std::print("{}", text);
    }

    void TerminalUi::WriteLine(const std::string& text) {
        std::println("{}", text);
    }

    void TerminalUi::WriteError(const std::string& text) {
        std::println("\033[31mError: {}\033[0m", text);
    }

    std::optional<std::string> TerminalUi::ReadLine(const std::string& prompt) {
        std::print("\033[32m{}\033[0m", prompt);

        std::string input;
        if (!std::getline(std::cin, input)) {
            return std::nullopt;
        }

        auto trimmed = Trim(input);
        if (trimmed == kBlockDelimiterStart) {
            return ReadBlockFromStdin();
        }

        if (trimmed.ends_with(kBlockDelimiterStart)) {
            auto headerView = trimmed.substr(0, trimmed.size() - kBlockDelimiterStart.size());
            std::string header{ Trim(headerView) };

            auto block = ReadBlockFromStdin();
            if (!block.has_value()) {
                return std::nullopt;
            }

            if (header.empty()) {
                return block.value();
            }

            return header + "\n" + block.value();
        }

        return input;
    }

}

/*
Uses C++23 `std::print` and `std::println` for efficient formatted output.
Includes basic ANSI color codes for prompts (Green) and errors (Red) to enhance UX.
Adds multiline input support using `<<` and `>>` delimiters and a continuation prompt.
*/
