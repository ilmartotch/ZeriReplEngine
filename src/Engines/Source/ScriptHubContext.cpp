#include "../Include/ScriptHubContext.h"
#include "../../Core/Include/HelpCatalog.h"

#include <string>

namespace Zeri::Engines::Defaults {

    void ScriptHubContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("Code context active. Use /context to list reachable contexts, then switch with $<context>.");
    }

    ExecutionOutcome ScriptHubContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)state;
        (void)terminal;

        if (cmd.commandName == "help") {
            std::string output = "Code Context — Available Commands\n";
            output += "\n";
            output += "Global Commands:\n";
            output += "  /help — Show help for the active context\n";
            output += "  /context — List reachable contexts from here\n";
            output += "  /back — Return to previous context\n";
            output += "  /save — Save session state to disk\n";
            output += "  /status — Show engine diagnostics\n";
            output += "  /reset — Reset the current session\n";
            output += "  /exit — Exit the REPL\n";
            output += "\n";
            output += "Context Navigation:\n";
            output += "  $lua — Enter Lua context\n";
            output += "  $python — Enter Python context\n";
            output += "  $js — Enter JavaScript context\n";
            output += "  $ts — Enter TypeScript context\n";
            output += "  $ruby — Enter Ruby context\n";
            output += "  $global — Return to root context\n";
            output += "\n";
            output += "Note: language entry uses $<context>, not /<command>.";
            return output;
        }

        return std::unexpected(ExecutionError{
            "SCRIPTHUB_UNKNOWN_COMMAND",
            "Unknown command in code context: " + cmd.commandName,
            cmd.rawInput,
            { "Use /help to list available commands.",
              "Use /context and switch with $<context> for language contexts." }
        });
    }

}

/*
ScriptHubContext.cpp
Implements the `zeri::code>` hub as a context-navigation entry point.

Behavior:
  - OnEnter prints guidance for `/context` and `$<context>` navigation.
  - /help lists global commands and supported language context switches
    using `$lua`, `$python`, `$js`, `$ts`, `$ruby`.
  - Slash language commands are intentionally unsupported in this context.
*/
