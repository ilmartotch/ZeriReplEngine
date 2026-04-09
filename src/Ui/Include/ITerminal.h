#pragma once
#include <string>
#include <optional>
#include <vector>

namespace Zeri::Ui {

    class ITerminal {
    public:
        virtual ~ITerminal() = default;

        virtual void Write(const std::string& text) = 0;
        virtual void WriteLine(const std::string& text) = 0;
        virtual void WriteError(const std::string& text) = 0;
        virtual void WriteSuccess(const std::string& text) = 0;
        virtual void WriteInfo(const std::string& text) = 0;
        
        [[nodiscard]] virtual std::optional<std::string> ReadLine(const std::string& prompt) = 0;

        [[nodiscard]] virtual bool Confirm(const std::string& prompt, bool default_value = true) = 0;
        [[nodiscard]] virtual std::optional<int> SelectMenu(const std::string& title, const std::vector<std::string>& options) = 0;
    };

}

/*
ITerminal.h — Abstract user interaction layer.

Responsabilità:
  - Write/WriteLine/WriteError/WriteSuccess/WriteInfo: Output methods.
  - ReadLine: Interactive input with prompt.
  - Confirm/SelectMenu: Wizard interaction methods for guided flows.

Allows switching between console I/O, bridge (headless), or GUI
without changing the core engine logic.

Dipendenze: nessuna (pure interface).
*/
