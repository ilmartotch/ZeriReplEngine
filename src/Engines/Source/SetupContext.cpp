#include "../Include/SetupContext.h"
#include "../../Core/Include/RuntimeState.h"

namespace Zeri::Engines::Defaults {

    void SetupContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        (void)terminal;
        terminal.WriteLine("--- Configuration Wizard ---");
        terminal.WriteLine("Type /start to begin the setup or /back to skip.");
    }

    ExecutionOutcome SetupContext::HandleCommand(
        const std::string& commandName,
        const std::vector<std::string>& args,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)args;
        if (commandName == "start") {
            RunWizard(terminal, state);
            return "Configuration complete. Returning to global context.";
        }

        return std::unexpected(ExecutionError{"SETUP_ERR", "Unknown setup command. Use /start."});
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

        // Auto-exit wizard
        state.PopContext();
    }

}

/*
FILE DOCUMENTATION:
SetupContext Implementation.
Implements the interactive wizard logic.
Results are stored in the Global RuntimeState, making them accessible to the SandboxContext for the /edit command.
*/
