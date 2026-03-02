#include "../Include/SetupContext.h"
#include "../../Core/Include/RuntimeState.h"

namespace Zeri::Engines::Defaults {

    void SetupContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        (void)terminal;
        terminal.WriteLine("--- Configuration Wizard ---");
        terminal.WriteLine("Type /help for commands.");
    }

    ExecutionOutcome SetupContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {

        if (cmd.commandName == "help") {
            return
                "Setup Context Help\n"
                "------------------\n"
                "Commands\n"
                "  /start     Run setup wizard\n"
                "\n"
                "Example\n"
                "  $setup | /start\n";
        }

        if (cmd.commandName == "start") {
            RunWizard(terminal, state);
            return "Configuration complete. Returning to global context.";
        }

        return std::unexpected(ExecutionError{ "SETUP_ERR", "Unknown setup command. Use /help or /start." });
    }

    void SetupContext::RunWizard(Zeri::Ui::ITerminal& terminal, Zeri::Core::RuntimeState& state) {
        terminal.WriteLine("\n[1/1] Preferred Code Editor");
        terminal.WriteLine("1. Visual Studio Code (code)");
        terminal.WriteLine("2. Notepad++ (notepad++)");
        terminal.WriteLine("3. Sublime Text (subl)");
        terminal.WriteLine("4. Custom/System Default");

        auto choice = terminal.ReadLine("Select option (1-4): ");
        std::string ide = "default";

        if (choice == "1") ide = "code";
        else if (choice == "2") ide = "notepad++";
        else if (choice == "3") ide = "subl";

        state.SetGlobalVariable("preferred_ide", ide);
        terminal.WriteLine("IDE set to: " + ide);

        state.PopContext();
    }

}

/*
Implements the interactive wizard logic.
Results are stored in the Global RuntimeState, making them accessible to the SandboxContext for the /edit command.
*/
