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
        
        // Wizard methods
        [[nodiscard]] virtual bool Confirm(const std::string& prompt, bool default_value = true) = 0;
        [[nodiscard]] virtual std::optional<int> SelectMenu(const std::string& title, const std::vector<std::string>& options) = 0;
    };

}

/*
Interface `ITerminal`.
Abstracts the user interaction layer. This allows switching between standard console I/O,
a GUI console, or a network socket without changing the core engine logic.
*/
