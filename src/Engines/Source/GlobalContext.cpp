#include "../Include/GlobalContext.h"
#include "../../Core/Include/RuntimeState.h"

namespace Zeri::Engines::Defaults {

    void GlobalContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        (void)terminal;
        terminal.WriteLine("Entering Global Context. Type $help for system info.");
    }

    ExecutionOutcome GlobalContext::HandleCommand(
        const std::string& commandName,
        const std::vector<std::string>& args,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)args;
        (void)terminal;
        if (commandName == "exit") {
            state.RequestExit();
            return "Shutdown requested.";
        }
        if (commandName == "help") {
            return "GLOBAL HELP: Use $context to switch, /command to execute.\n" 
                   "Commands: /exit, /help, /back, /save";
        }
        if (commandName == "back") {
            state.PopContext();
            return "Returned to previous context.";
        }
        
        return std::unexpected(ExecutionError{"GlobalUnknown", "Unknown global command."});
    }

}
