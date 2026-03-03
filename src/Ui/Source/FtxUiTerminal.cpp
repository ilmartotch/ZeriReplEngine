#include "../Include/FtxUiTerminal.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <algorithm>

namespace Zeri::Ui {

    using namespace ftxui;

    FtxUiTerminal::FtxUiTerminal() 
        : m_screen(ScreenInteractive::Fullscreen()) {
    }

    void FtxUiTerminal::Write(const std::string& text) {
        std::lock_guard lock(m_mutex);
        if (!m_history.empty()) {
            m_history.back().text += text;
        } else {
            m_history.push_back({text, Color::Default});
        }
    }

    void FtxUiTerminal::WriteLine(const std::string& text) {
        std::lock_guard lock(m_mutex);
        m_history.push_back({text, Color::Default});
    }

    void FtxUiTerminal::WriteError(const std::string& text) {
        std::lock_guard lock(m_mutex);
        m_history.push_back({"Error: " + text, Color::Red});
    }

    std::optional<std::string> FtxUiTerminal::ReadLine(const std::string& prompt) {
        m_currentPrompt = prompt;
        m_inputBuffer.clear();

        std::string result;
        bool submitted = false;

        Component input_option = Input(&m_inputBuffer, "Type a command...");
        
        // Custom event handling for Enter
        auto input_with_event = CatchEvent(input_option, [&](Event event) {
            if (event == Event::Return) {
                result = m_inputBuffer;
                submitted = true;
                m_screen.ExitLoopClosure()();
                return true;
            }
            return false;
        });

        auto component = Container::Vertical({
            Renderer([&] {
                std::lock_guard lock(m_mutex);
                Elements elements;
                // Limit history display for performance if needed
                for (const auto& msg : m_history) {
                    elements.push_back(paragraph(msg.text) | color(msg.color));
                }
                return vbox(std::move(elements)) | vscroll_indicator | frame | flex;
            }),
            Renderer([&](bool) {
                return separator();
            }),
            Container::Horizontal({
                Renderer([&] { return text(m_currentPrompt) | bold | color(Color::Cyan); }),
                input_with_event | flex
            })
        });

        m_screen.Loop(component);

        if (!submitted) {
            return std::nullopt;
        }

        // Echo the command to history to maintain context
        {
            std::lock_guard lock(m_mutex);
            m_history.push_back({m_currentPrompt + result, Color::GrayDark});
        }

        return result;
    }

    void FtxUiTerminal::AddMessage(std::string text, ftxui::Color color) {
        std::lock_guard lock(m_mutex);
        m_history.push_back({std::move(text), color});
    }

}
