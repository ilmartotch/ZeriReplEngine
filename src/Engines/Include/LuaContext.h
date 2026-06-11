#pragma once

#include "BaseContext.h"
#include "Interface/IExecutor.h"

#include <memory>
#include <string>

namespace Zeri::Engines::Defaults {

    class LuaContext : public BaseContext {
    public:
        LuaContext();

        [[nodiscard]] std::string GetName() const override { return "lua"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::lua>"; }

        void OnEnter(Zeri::Ui::ITerminal& terminal) override;
        [[nodiscard]] bool IsInitialized() const override { return m_initialized; }

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;
        [[nodiscard]] bool RequestCancel() override;

    private:
        std::shared_ptr<IExecutor> m_executor;
        bool m_initialized{ false };
    };

}

/*
LuaContext.h
Dedicated Lua REPL context with `zeri::lua>` prompt and CRUD/editor commands.
The context keeps a shared `LuaExecutor` built from detected runtime data
(SystemGuard) and reuses it for direct execution, saved scripts, and ScriptEditorContext.
*/
