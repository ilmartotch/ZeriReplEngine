#include "../Include/BuiltinExecutor.h"
#include "../Include/Interface/IContext.h"

namespace Zeri::Engines::Defaults {

    ExecutionOutcome BuiltinExecutor::Execute(const Command& cmd, Zeri::Core::RuntimeState& state) {
        if (cmd.commandName == "exit") {
            state.RequestExit();
            return "Exiting...";
        }

        if (cmd.commandName == "help") {
            return
                "Zeri REPL Help\n"
                "================\n"
                "Core syntax\n"
                "  /<command>            Execute a command in current context\n"
                "  $<context>            Switch context (global, math, sandbox, setup)\n"
                "  <stage1> | <stage2>   Pipeline across stages\n"
                "\n"
                "Global built-in commands\n"
                "  /help                 Show this help\n"
                "  /exit                 Exit REPL\n"
                "  /set <key> <value>    Store variable\n"
                "  /get <key>            Read variable\n"
                "  /lua <script>         Execute inline Lua code\n";
        }

        auto scope = Zeri::Core::RuntimeState::VariableScope::Global;
        std::string scopeLabel = "global";

        if (auto* ctx = state.GetCurrentContext(); ctx && ctx->GetName() != "global") {
            scope = Zeri::Core::RuntimeState::VariableScope::Local;
            scopeLabel = "local";
        }

        if (cmd.commandName == "set") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "MissingArguments",
                    "Missing arguments for set",
                    cmd.rawInput,
                    { "Usage: /set <key> <value>" }
                });
            }

            std::string value;
            if (cmd.args.size() >= 2) {
                value = cmd.args[1];
            } else if (cmd.pipeInput.has_value()) {
                value = *cmd.pipeInput;
            } else {
                return std::unexpected(ExecutionError{
                    "MissingArguments",
                    "Missing value for set",
                    cmd.rawInput,
                    { "Usage: /set <key> <value>", "Or provide value via pipeline." }
                });
            }

            state.SetVariable(scope, cmd.args[0], value);
            return "Variable set (" + scopeLabel + "): " + cmd.args[0];
        }

        if (cmd.commandName == "get") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "MissingArguments",
                    "Missing key for get",
                    cmd.rawInput,
                    { "Usage: /get <key>" }
                });
            }

            auto val = state.GetVariable(scope, cmd.args[0]);
            if (!val.has_value()) {
                return "Variable not found.";
            }

            try {
                return std::any_cast<std::string>(val);
            } catch (...) {
                return "Value is not a string.";
            }
        }

        return std::unexpected(ExecutionError{
            "UnknownBuiltin",
            "Command not implemented.",
            cmd.rawInput
        });
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
