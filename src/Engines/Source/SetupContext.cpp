#include "../Include/SetupContext.h"
#include "../../Core/Include/RuntimeState.h"
#include "../../Core/Include/Strings.h"
#include <vector>
#include <string>

namespace Zeri::Engines::Defaults {

    using namespace Zeri::Ui::Config;

    void SetupContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo(std::string(Strings::SetupTitle));
        terminal.WriteLine("Type /start to begin configuration.");
    }

    ExecutionOutcome SetupContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {

        if (cmd.commandName == "help") {
            return
                "Setup Context — Available Commands\n"
                "\n"
                "Global Commands:\n"
                "  /help                    — Show help for the active context\n"
                "  /context                 — List available contexts\n"
                "  /back                    — Return to previous context\n"
                "  /save                    — Save session state to disk\n"
                "  /exit                    — Exit the REPL\n"
                "\n"
                "Setup Commands:\n"
                "  /start                   — Run the configuration wizard\n"
                "\n"
                "The wizard guides you through:\n"
                "  1. Preferred code editor selection\n"
                "  2. IDE configuration save\n"
                "\n"
                "Example:\n"
                "  $setup | /start          — Enter setup and start wizard";
        }

        if (cmd.commandName == "start") {
            RunWizard(terminal, state);
            return std::string(Strings::SetupComplete);
        }

        return std::unexpected(ExecutionError{ "SETUP_ERR", std::string(Strings::UnknownCmd) });
    }

    void SetupContext::RunWizard(Zeri::Ui::ITerminal& terminal, Zeri::Core::RuntimeState& state) {
        std::vector<std::string> editors = {
            "Visual Studio Code (code)",
            "Notepad++ (notepad++)",
            "Sublime Text (subl)",
            "Custom/System Default"
        };

        auto choiceIdx = terminal.SelectMenu(std::string(Strings::SetupEditor), editors);
        
        std::string ide = "default";
        if (choiceIdx.has_value()) {
            switch (*choiceIdx) {
                case 0: ide = "code"; break;
                case 1: ide = "notepad++"; break;
                case 2: ide = "subl"; break;
                default: ide = "default"; break;
            }
        }

        if (terminal.Confirm("Do you want to set '" + ide + "' as your preferred editor?")) {
            state.SetGlobalVariable("preferred_ide", ide);
            terminal.WriteSuccess("IDE preference saved: " + ide);
        } else {
            terminal.WriteInfo(std::string(Strings::WizardCancel));
        }

        state.PopContext();
    }

}

/*
SetupContext.cpp — Configuration wizard context.

  - /start: Launches the interactive wizard (SelectMenu + Confirm) for
    setting the preferred editor. Saves to global variable "preferred_ide".
  - /help: Formatted list of setup-specific commands.
  - RunWizard: drives the wizard flow and pops the context on completion.

QA Changes:
  - /help reformatted with "command — description" layout.
*/
