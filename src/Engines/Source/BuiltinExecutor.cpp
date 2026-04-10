#include "../Include/BuiltinExecutor.h"
#include "../Include/Interface/IContext.h"
#include "../../Core/Include/HelpCatalog.h"

#include <string>

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
            std::string output;
            output += "Zeri REPL — Available Commands\n";
            output += "\n";
            output += "Syntax:\n";
            output += "/<command> Execute a command in the current context\n";
            output += "$<context> Switch context using reachable targets from /context\n";
            output += "!<shell_command> Execute a system shell command\n";
            output += "<expr> Evaluate an expression (context-dependent)\n";
            output += "<stage1> | <stage2> Pipeline output across stages\n";
            output += "# comment Inline comment (ignored by parser)\n";
            output += "\n";
            output += "Global Commands:\n";

            const auto& globalCommands = Zeri::Core::HelpCatalog::Instance().CommandsForGroup("global");
            for (const auto& command : globalCommands) {
                output += "  ";
                output += command.command;
                output += " — ";
                output += command.synopsis;
                output += "\n";
            }

            output += "\n";
            output += "Contexts:\n";
            const auto reachable = Zeri::Core::HelpCatalog::Instance().ReachableFrom("global");
            for (const auto& contextName : reachable) {
                const auto* context = Zeri::Core::HelpCatalog::Instance().FindContext(contextName);
                if (context == nullptr) {
                    continue;
                }

                output += "  $";
                output += context->name;
                output += " — ";
                output += context->description;
                output += "\n";
            }

            output += "\nType /help inside a context for context-specific commands.";
            return output;
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
