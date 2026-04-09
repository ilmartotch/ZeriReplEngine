#include "../Include/ScriptHubContext.h"

#include "../Include/JsContext.h"
#include "../Include/LuaContext.h"
#include "../Include/PythonContext.h"
#include "../Include/RubyContext.h"
#include "../../Core/Include/SystemGuard.h"

#include <memory>
#include <string>
#include <string_view>

namespace {

    [[nodiscard]] std::string RuntimeLine(
        const Zeri::Core::SystemHealth& health,
        std::string_view language
    ) {
        const auto* runtime = health.GetRuntime(std::string(language));
        if (runtime != nullptr && runtime->available) {
            return "  [OK] " + std::string(language) + "\n";
        }
        return "[--] " + std::string(language) + " — engine not found in Zeri environment.\n"
               "-> " + Zeri::Core::SystemGuard::GetInstallHint(language) + "\n";
    }

    [[nodiscard]] std::string TsRuntimeLine(const Zeri::Core::SystemHealth& health) {
        const auto* runtime = health.GetRuntime("js");
        if (runtime != nullptr && runtime->available) {
            return "  [OK] ts\n";
        }
        return "[--] ts — engine not found in Zeri environment.\n"
               "-> " + Zeri::Core::SystemGuard::GetInstallHint("ts") + "\n";
    }

    [[nodiscard]] Zeri::Engines::ExecutionOutcome PushAndEnter(
        std::unique_ptr<Zeri::Engines::IContext> next,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        state.PushContext(std::move(next));
        auto* active = state.GetCurrentContext();
        if (active == nullptr) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "SCRIPTHUB_INTERNAL_ERR",
                "Failed to activate target context."
            });
        }
        active->OnEnter(terminal);
        return "Context switched to: " + active->GetName();
    }

}

namespace Zeri::Engines::Defaults {

    void ScriptHubContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        const auto health = Zeri::Core::SystemGuard::CheckEnvironment();

        std::string info = "Zeri::Code - available languages:\n";
        info += RuntimeLine(health, "lua");
        info += RuntimeLine(health, "python");
        info += RuntimeLine(health, "js");
        info += TsRuntimeLine(health);
        info += RuntimeLine(health, "ruby");

        terminal.WriteLine(info);
    }

    ExecutionOutcome ScriptHubContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (cmd.commandName == "help") {
            const auto health = Zeri::Core::SystemGuard::CheckEnvironment();
            std::string output = "ScriptHub commands:\n";
            output += " /lua -> LuaContext (luajit)\n";
            output += " /python -> PythonContext (python3)\n";
            output += " /js -> JsContext (bun)\n";
            output += " /ts -> JsContext (bun)\n";
            output += " /ruby -> RubyContext (ruby 3.3+ YJIT)\n\n";
            output += "Runtime status:\n";
            output += RuntimeLine(health, "lua");
            output += RuntimeLine(health, "python");
            output += RuntimeLine(health, "js");
            output += TsRuntimeLine(health);
            output += RuntimeLine(health, "ruby");
            return output;
        }

        if (cmd.commandName == "lua") {
            return PushAndEnter(std::make_unique<LuaContext>(), state, terminal);
        }

        if (cmd.commandName == "python") {
            return PushAndEnter(std::make_unique<PythonContext>(), state, terminal);
        }

        if (cmd.commandName == "js") {
            return PushAndEnter(std::make_unique<JsContext>(false), state, terminal);
        }

        if (cmd.commandName == "ts") {
            return PushAndEnter(std::make_unique<JsContext>(true), state, terminal);
        }

        if (cmd.commandName == "ruby") {
            return PushAndEnter(std::make_unique<RubyContext>(), state, terminal);
        }

        return std::unexpected(ExecutionError{
            "SCRIPTHUB_UNKNOWN_LANG",
            "Unknown language command for ScriptHub: " + cmd.commandName,
            cmd.rawInput,
            { "Use /help to list supported languages." }
        });
    }

}

/*
ScriptHubContext.cpp
Implements the `zeri::code>` hub context with pure dispatch to language
contexts: /lua, /python, /js, /ts, /ruby. OnEnter and /help report runtime
status through SystemGuard using standardized messages.

Runtime status UX rules:
  - Never expose runtime binary paths to the user.
  - Show only [OK]/[--] language status.
  - For missing engines, include wizard-style setup hints from
    SystemGuard::GetInstallHint().

Runtime policy:
  - /js and /ts both execute through Bun.
  - Lua uses luajit, Python uses python3, Ruby uses ruby (3.3+ YJIT).
*/
