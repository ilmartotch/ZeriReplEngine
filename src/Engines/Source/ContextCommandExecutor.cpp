#include "../Include/ContextCommandExecutor.h"
#include "../Include/ExpressionExecutor.h"

namespace Zeri::Engines::Defaults {

    ExecutionOutcome ContextCommandExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal&
    ) {
        if (cmd.commandName == "math") {
            state.SetActiveContext("math");
            return "Context switched to: MATH";
        }

        if (cmd.commandName == "@context_eval" && state.GetActiveContext() == "math") {
            return ExpressionExecutor::Evaluate(cmd.args[0], state);
        }

        return std::unexpected(ExecutionError{
            "WrongContext",
            "Input non valido per il contesto: " + state.GetActiveContext()
        });
    }

    ExecutionType ContextCommandExecutor::GetType() const {
        return ExecutionType::Builtin;
    }
}

/*
Implementation of `ContextCommandExecutor`.
Stub for context activation commands.

Supported contexts:
- /math: activates expression evaluation context
- /script: activates script loading context
- /exit: pops current context

(Integrate context rules with parser for context-specific syntax validation
for the next update)
*/