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

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

    private:
        std::shared_ptr<IExecutor> m_executor;
    };

}

/*
LuaContext.h
Contesto REPL Lua dedicato con prompt `zeri::lua>` e comandi CRUD/editoriali.
Il contesto mantiene un `LuaExecutor` condiviso costruito da runtime rilevato
(SystemGuard) e lo riusa per esecuzioni dirette, script salvati e ScriptEditorContext.
*/
