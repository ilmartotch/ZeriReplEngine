#include "../Include/TerminalUi.h"
#include <iostream>
#include <print>

namespace Zeri::Ui {

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
        if (std::getline(std::cin, input)) {
            return input;
        }
        return std::nullopt;
    }

}

/*
Implementation of `TerminalUi`.
Uses C++23 `std::print` and `std::println` for efficient formatted output.
Includes basic ANSI color codes for prompts (Green) and errors (Red) to enhance UX.
*/
