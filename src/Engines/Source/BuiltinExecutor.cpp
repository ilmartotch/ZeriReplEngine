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
                "  /set <key> <value>    Store variable (session in global, local in non-global)\n"
                "  /get <key>            Read variable from current scope\n"
                "\n"
                "Pipeline notes\n"
                "  - Pipeline output is carried as text to the next stage.\n"
                "  - Latest value is persisted in session key: __pipe_value\n"
                "  - In math context, /calc can fallback to __pipe_value when args are missing.\n"
                "\n"
                "Examples\n"
                "  $math\n"
                "  /calc 2 + 3\n"
                "  /logic and true false\n"
                "  /set x 10\n"
                "  /get x\n"
                "  /set expr \"10 + 5\" | $math | /calc\n"
                "  $sandbox | /list\n";
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

        return std::unexpected(ExecutionError{ "UnknownBuiltin", "Command not implemented yet." });
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
