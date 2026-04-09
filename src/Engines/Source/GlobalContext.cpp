#include "../Include/GlobalContext.h"
#include "../../Core/Include/RuntimeState.h"
#include "../../Core/Include/SystemGuard.h"

#include <sstream>

namespace {
    [[nodiscard]] std::string JoinArgs(const std::vector<std::string>& args) {
        std::ostringstream stream;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                stream << ' ';
            }
            stream << args[i];
        }
        return stream.str();
    }

    [[nodiscard]] Zeri::Core::ScriptRuntime ResolveLuaRuntime() {
        const auto health = Zeri::Core::SystemGuard::CheckEnvironment();
        if (const auto* runtime = health.GetRuntime("lua"); runtime != nullptr) {
            return *runtime;
        }

        Zeri::Core::ScriptRuntime fallback;
        fallback.language = "lua";
        return fallback;
    }
}

namespace Zeri::Engines::Defaults {

    void GlobalContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("Global Context active. Type /help for available commands.");
    }

    ExecutionOutcome GlobalContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (cmd.commandName == "back") {
            return "Already at root context.";
        }

        if (cmd.type == InputType::Expression) {
            return std::unexpected(ExecutionError{
                "ExpressionInGlobal",
                "Free-form expressions are not supported in global context.",
                cmd.args.empty() ? "" : cmd.args[0],
                { "Switch to math context: $math",
                  "Or evaluate inline:     $math | <expression>" }
            });
        }

        if (cmd.commandName == "lua") {
            if (!m_luaExecutor) {
                m_luaExecutor = std::make_unique<LuaExecutor>(ResolveLuaRuntime());
            }

            Command luaCommand = cmd;
            if (!luaCommand.args.empty()) {
                luaCommand.rawInput = JoinArgs(luaCommand.args);
            } else if (luaCommand.pipeInput.has_value()) {
                luaCommand.rawInput = *luaCommand.pipeInput;
            }
            return m_luaExecutor->Execute(luaCommand, state, terminal);
        }

        return m_builtinExecutor.Execute(cmd, state, terminal);
    }

}

/*
GlobalContext.cpp — Root context router.

  - /back is handled inline (no-op at root, returns informational message).
  - /lua is delegated to the LuaExecutor member.
  - All other commands (/help, /exit, /set, /get) go to BuiltinExecutor.
  - Expression inputs receive an error with hints (redirect to $math).

QA Changes:
  - OnEnter uses WriteInfo for a single horizontal welcome line.
*/
