#pragma once
#include "ITerminal.h"

namespace Zeri::Ui {

    class TerminalUi : public ITerminal {
    public:
        void Write(const std::string& text) override;
        void WriteLine(const std::string& text) override;
        void WriteError(const std::string& text) override;
        [[nodiscard]] std::optional<std::string> ReadLine(const std::string& prompt) override;
    };

}

/*
Header for `TerminalUi`.
Concrete implementation of `ITerminal` for standard input/output streams (stdin/stdout).
*/
