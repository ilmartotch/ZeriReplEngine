#pragma once

#include "BaseContext.h"
#include "Interface/IExecutor.h"

#include <memory>
#include <string>

namespace Zeri::Engines::Defaults {

    class JsContext : public BaseContext {
    public:
        explicit JsContext(bool typescript = false);

        [[nodiscard]] std::string GetName() const override { return m_typescript ? "ts" : "js"; }
        [[nodiscard]] std::string GetPrompt() const override { return m_typescript ? "zeri::ts>" : "zeri::js>"; }

        void OnEnter(Zeri::Ui::ITerminal& terminal) override;

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

    private:
        [[nodiscard]] std::string LanguageKey() const { return m_typescript ? "ts" : "js"; }
        [[nodiscard]] std::string DisplayName() const { return m_typescript ? "TypeScript" : "JavaScript"; }

        std::shared_ptr<IExecutor> m_executor;
        bool m_typescript{ false };
    };

}

/*
JsContext.h
Contesto REPL JS/TS configurabile via flag `typescript`, con prompt dinamico
`zeri::js>` o `zeri::ts>`. Mantiene executor condiviso e fornisce comando
workflow editor/CRUD in parità funzionale con LuaContext/PythonContext.
*/
