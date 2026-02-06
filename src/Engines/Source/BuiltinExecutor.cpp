#include "../Include/BuiltinExecutor.h"

namespace Zeri::Engines::Defaults {

    ExecutionOutcome BuiltinExecutor::Execute(
        const Command& cmd, 
        Zeri::Core::RuntimeState& state
    ) {
        if (cmd.target == "exit") {
            state.RequestExit();
            return "Exiting...";
        }
        if (cmd.target == "help") {
            return "Available commands: help, exit, set <key> <val>, get <key>, *.lua, !<command>";
        }
        if (cmd.target == "set" && cmd.args.size() >= 2) {
            state.SetGlobalVariable(cmd.args[0], cmd.args[1]);
            return "Variable set: " + cmd.args[0];
        }
        if (cmd.target == "get" && !cmd.args.empty()) {
            auto val = state.GetGlobalVariable(cmd.args[0]);
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
