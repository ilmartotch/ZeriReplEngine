#include "../Include/GlobalContext.h"
#include "../Include/BuiltinExecutor.h"
#include "../../Core/Include/RuntimeState.h"

namespace Zeri::Engines::Defaults {

    void GlobalContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteLine("Entering Global Context. Type /help for available commands.");
    }

    ExecutionOutcome GlobalContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)terminal;

        if (cmd.commandName == "back") {
            state.PopContext();
            return "Returned to previous context.";
        }

        BuiltinExecutor executor;
        return executor.Execute(cmd, state);
    }

}
