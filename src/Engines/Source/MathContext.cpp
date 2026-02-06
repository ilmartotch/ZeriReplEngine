#include "../Include/MathContext.h"
#include "../../Core/Include/RuntimeState.h"

namespace Zeri::Engines::Defaults {

    void MathContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        (void)terminal;
        terminal.WriteLine("--- Math Engine Activated ---");
        terminal.WriteLine("Type /calc to start or /exit to quit.");
    }

    ExecutionOutcome MathContext::HandleCommand(
        const std::string& commandName,
        const std::vector<std::string>& args,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)args;
        if (commandName == "calc") {
            return "Logic for math calculation would go here.";
        }

        // Example of a WIZARD / Interactivity
        if (commandName == "saveResult") {
            auto ans = terminal.ReadLine("Do you want to save the last result to GLOBAL? (y/n): ");
            if (ans == "y") {
                state.SetGlobalVariable("last_math_res", std::string(""));
                return "Result promoted to Global State.";
            }
            return "Result kept local.";
        }

        return std::unexpected(ExecutionError{"MathErr", "Unknown math command."});
    }

}
