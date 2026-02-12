#include "../Include/BuiltinExecutor.h"
#include "../Include/Interface/IContext.h"

namespace Zeri::Engines::Defaults {

    ExecutionOutcome BuiltinExecutor::Execute(
        const Command& cmd, 
        Zeri::Core::RuntimeState& state
    ) {
        if (cmd.commandName == "exit") {
            state.RequestExit();
            return "Exiting...";
        }

        if (cmd.commandName == "help") {
            return "Available commands: help, exit, set <key> <val>, get <key>, *.lua, !<command>";
        }

        auto scope = Zeri::Core::RuntimeState::VariableScope::Session;
        std::string scopeLabel = "session";
        if (auto* ctx = state.GetCurrentContext(); ctx && ctx->GetName() != "global") {
            scope = Zeri::Core::RuntimeState::VariableScope::Local;
            scopeLabel = "local";
        }

        if (cmd.commandName == "set") {
            if (cmd.args.size() < 2) {
                return std::unexpected(ExecutionError{
                    "MissingArguments",
                    "Missing arguments for set",
                    cmd.rawInput,
                    { "Usage: /set <key> <value>" }
                });
            }
            state.SetVariable(scope, cmd.args[0], cmd.args[1]);
            return "Variable set (" + scopeLabel + "): " + cmd.args[0];
        }

        if (cmd.commandName == "get" && !cmd.args.empty()) {
            auto val = state.GetVariable(scope, cmd.args[0]);
            if (val.has_value()) {
                try {
                    return std::any_cast<std::string>(val);
                } catch (...) {
                    return "Value is not a string.";
                }
            }
            return "Variable not found.";
        }

        return std::unexpected(ExecutionError{"UnknownBuiltin", "Command not implemented yet."});
    }

    ExecutionType BuiltinExecutor::GetType() const {
        return ExecutionType::Builtin;
    }

}

/*
Implementation of `BuiltinExecutor`.
Contains logic for:
- `exit`: Sets the exit flag in RuntimeState.
- `set`/`get`: Manipulates the RuntimeState variable map.
- `help`: Returns static help text.
*/
