#pragma once

#include <stdexcept>

namespace Zeri::Core {

    class InterruptException : public std::exception {
    public:
        [[nodiscard]] const char* what() const noexcept override {
            return "Operation interrupted by user (Ctrl+C).";
        }
    };

}

/*
Custom exception thrown when the user presses Ctrl+C in the TUI.
Propagates through the worker thread's call stack, unwinding any active
HandleCommand execution and returning control to the main REPL loop
without terminating the application.
Engine code should NOT catch std::exception broadly; let InterruptException
pass through or catch and re-throw it explicitly.
*/