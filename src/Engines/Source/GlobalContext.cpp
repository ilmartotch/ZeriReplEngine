#include "../Include/GlobalContext.h"
#include "../../Core/Include/RuntimeState.h"

namespace Zeri::Engines::Defaults {

    void GlobalContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        // Lazy-init LuaExecutor (requires terminal reference)
        if (!m_luaExecutor) {
            m_luaExecutor = std::make_unique<LuaExecutor>(terminal);
        }

        terminal.WriteLine("Entering Global Context. Type /help for available commands.");
    }

    ExecutionOutcome GlobalContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (cmd.commandName == "back") {
            return "Already at root context.";
        }

        // Route /lua to the Lua execution engine
        if (cmd.commandName == "lua") {
            if (!m_luaExecutor) {
                m_luaExecutor = std::make_unique<LuaExecutor>(terminal);
            }
            return m_luaExecutor->Execute(cmd, state);
        }

        // Delegate all other commands to BuiltinExecutor
        return m_builtinExecutor.Execute(cmd, state);
    }

}

/*
GlobalContext acts as the root router:
- /back is handled inline (no-op at root).
- /lua is delegated to the LuaExecutor member (lazy-initialized on first OnEnter).
- All other commands (/help, /exit, /set, /get) are delegated to BuiltinExecutor.
*/
