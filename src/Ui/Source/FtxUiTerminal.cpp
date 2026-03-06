#include "../Include/FtxUiTerminal.h"
#include "../../Core/Include/Strings.h"
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <ftxui/dom/elements.hpp>
#include <algorithm>

namespace Zeri::Ui {

    using namespace ftxui;
    using namespace Zeri::Ui::Config;

    FtxUiTerminal::FtxUiTerminal() 
        : m_screen(ScreenInteractive::Fullscreen()) {
    }

    void FtxUiTerminal::Write(const std::string& text) {
        std::lock_guard lock(m_mutex);
        if (!m_history.empty()) {
            m_history.back().text += text;
        } else {
            AddMessage(text);
        }
    }

    void FtxUiTerminal::WriteLine(const std::string& text) {
        AddMessage(text);
    }

    void FtxUiTerminal::WriteError(const std::string& text) {
        AddMessage(text, Styles::Error.color, Styles::Error.bold, Styles::Error.prefix);
    }

    void FtxUiTerminal::WriteSuccess(const std::string& text) {
        AddMessage(text, Styles::Success.color, Styles::Success.bold, Styles::Success.prefix);
    }

    void FtxUiTerminal::WriteInfo(const std::string& text) {
        AddMessage(text, Styles::Info.color, Styles::Info.bold, Styles::Info.prefix);
    }

    void FtxUiTerminal::AddMessage(std::string text, ftxui::Color color, bool bold, std::string_view prefix) {
        std::lock_guard lock(m_mutex);
        m_history.push_back({std::move(text), color, bold, prefix});
    }

    std::optional<std::string> FtxUiTerminal::ReadLine(const std::string& prompt) {
        m_currentPrompt = prompt;
        m_inputBuffer.clear();

        std::string result;
        bool submitted = false;

        Component input_option = Input(&m_inputBuffer, std::string(Strings::DefaultPrompt));
        
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
                for (const auto& msg : m_history) {
                    Element line = text(std::string(msg.prefix)) | color(msg.color);
                    if (msg.bold) line = line | bold;
                    
                    Element content = paragraph(msg.text) | color(msg.color);
                    if (msg.bold) content = content | bold;

                    elements.push_back(hbox(line, content));
                }
                return vbox(std::move(elements)) | vscroll_indicator | frame | flex;
            }),
            Renderer([] { return separator(); }),
            Container::Horizontal({
                Renderer([&] { return text(m_currentPrompt) | bold | color(Color::Cyan); }),
                input_with_event | flex
            })
        });

        m_screen.Loop(component);

        if (!submitted) return std::nullopt;

        AddMessage(result, Styles::Command.color, false, Styles::Command.prefix);
        return result;
    }

    bool FtxUiTerminal::Confirm(const std::string& prompt, bool default_value) {
        bool result = default_value;
        bool submitted = false;

        auto component = Container::Vertical({
            Renderer([&] { return vbox({
                text(prompt) | bold,
                text(std::string(Strings::WizardSelect)) | dim
            }); }),
            Container::Horizontal({
                Button("Yes", [&] { result = true; submitted = true; m_screen.ExitLoopClosure()(); }),
                Button("No",  [&] { result = false; submitted = true; m_screen.ExitLoopClosure()(); })
            })
        }) | borderRounded | center;

        m_screen.Loop(component);
        return submitted ? result : default_value;
    }

    std::optional<int> FtxUiTerminal::SelectMenu(const std::string& title, const std::vector<std::string>& options) {
        int selected = 0;
        bool submitted = false;

        auto menu = Menu(&options, &selected);
        
        auto component = Container::Vertical({
            Renderer([&] { return text(title) | bold | color(Color::Yellow); }),
            Renderer([] { return separator(); }),
            menu,
            Renderer([] { return separator(); }),
            Button("Confirm", [&] { submitted = true; m_screen.ExitLoopClosure()(); })
        }) | borderRounded | center;

        m_screen.Loop(component);

        return submitted ? std::make_optional(selected) : std::nullopt;
    }

}
