#include "../Include/BuiltinExecutor.h"
#include "../Include/Interface/IContext.h"

namespace Zeri::Engines::Defaults {

    ExecutionOutcome BuiltinExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal&
    ) {
        if (cmd.commandName == "exit") {
            state.RequestExit();
            return "Exiting...";
        }

        if (cmd.commandName == "help") {
            return
                "Zeri REPL — Available Commands\n"
                "\n"
                "Syntax:\n"
                "/<command> Execute a command in the current context\n"
                "$<context> Switch context (global, math, sandbox, setup)\n"
                "!<shell_command> Execute a system shell command\n"
                "<expr> Evaluate an expression (context-dependent)\n"
                "<stage1> | <stage2> Pipeline output across stages\n"
                "# comment Inline comment (ignored by parser)\n"
                "\n"
                "Global Commands:\n"
                "/help — Show this help\n"
                "/context — List available contexts\n"
                "/status — Show session status\n"
                "/reset — Reset session (clear variables, return to global)\n"
                "/clear — Clear the screen\n"
                "/exit — Exit the REPL\n"
                "/back — Return to the previous context\n"
                "/save — Save session state to disk\n"
                "/set <key> <value> — Store a variable in the current scope\n"
                "/get <key> — Read a variable from the current scope\n"
                "/lua <script> — Execute inline Lua code\n"
                "\n"
                "Contexts:\n"
                "$code — Scripting language dispatch hub\n"
                "$math — Mathematical expression engine\n"
                "$sandbox — Module development environment\n"
                "$setup — Configuration wizard\n"
                "$global — Return to root context\n"
                "\n"
                "Type /help inside a context for context-specific commands.";
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
BuiltinExecutor.cpp — Handles global built-in commands.

Routes:
  - /exit: Sets the exit flag in RuntimeState.
  - /help: Returns formatted help text listing all syntax forms, global
    commands (including /status, /reset, /clear), and available contexts.
  - /set: Writes a variable to the current scope (local or global).
  - /get: Reads a variable from the current scope.

QA Changes:
  - /help text reformatted with structured sections (Syntax, Global Commands,
    Contexts) and consistent description alignment.
  - Added /status, /reset, /clear entries to Global Commands section.
  - /status and /reset are routed in HandleGlobalCommand (main.cpp) before
    reaching BuiltinExecutor; they appear here only in help text.
*/
