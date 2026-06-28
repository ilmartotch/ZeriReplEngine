#pragma once

#include "BaseContext.h"
#include "BuiltinExecutor.h"
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
        [[nodiscard]] bool IsInitialized() const override { return m_initialized; }

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;
        [[nodiscard]] bool RequestCancel() override;

    private:
        [[nodiscard]] std::string LanguageKey() const { return m_typescript ? "ts" : "js"; }
        [[nodiscard]] std::string DisplayName() const { return m_typescript ? "TypeScript" : "JavaScript"; }

        std::shared_ptr<IExecutor> m_executor;
        BuiltinExecutor m_builtinExecutor;
        bool m_typescript{ false };
        bool m_initialized{ false };
    };

}

/*
JsContext.h
Configurable JS/TS REPL context via the `typescript` flag, with dynamic prompt
`zeri::js>` or `zeri::ts>`. Maintains a shared executor and provides an
editor/CRUD command workflow aligned with LuaContext and PythonContext.
*/
