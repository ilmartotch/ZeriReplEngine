#pragma once
#include "ITerminal.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <vector>
#include <string>
#include <optional>
#include <mutex>

namespace Zeri::Ui {

    class FtxUiTerminal : public ITerminal {
    public:
        FtxUiTerminal();
        ~FtxUiTerminal() override = default;

        void Write(const std::string& text) override;
        void WriteLine(const std::string& text) override;
        void WriteError(const std::string& text) override;
        void WriteSuccess(const std::string& text) override;
        void WriteInfo(const std::string& text) override;

        [[nodiscard]] std::optional<std::string> ReadLine(const std::string& prompt) override;
        
        // Wizard methods
        [[nodiscard]] bool Confirm(const std::string& prompt, bool default_value = true) override;
        [[nodiscard]] std::optional<int> SelectMenu(const std::string& title, const std::vector<std::string>& options) override;

    private:
        struct Message {
            std::string text;
            ftxui::Color color;
            bool bold = false;
            std::string_view prefix;
        };

        void AddMessage(std::string text, ftxui::Color color = ftxui::Color::Default, bool bold = false, std::string_view prefix = "");
        
        std::vector<Message> m_history;
        std::string m_inputBuffer;
        std::string m_currentPrompt;
        std::mutex m_mutex;
        
        ftxui::ScreenInteractive m_screen;
    };

}
