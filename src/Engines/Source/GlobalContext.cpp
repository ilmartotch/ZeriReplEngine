#include "../Include/GlobalContext.h"
#include "../Include/BuiltinExecutor.h"
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
        (void)terminal;

        if (commandName == "exit" || commandName == "help" || commandName == "set") {
            Command cmd;
            cmd.type = Zeri::Engines::InputType::Command;
            cmd.commandName = commandName;
            cmd.args = args;
            cmd.rawInput = "/" + commandName;

            BuiltinExecutor builtin;
            return builtin.Execute(cmd, state);
        }

        if (commandName == "back") {
            state.PopContext();
            return "Returned to previous context.";
        }
        
        return std::unexpected(ExecutionError{"GlobalUnknown", "Unknown global command."});
    }

}
