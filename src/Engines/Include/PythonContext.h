#pragma once

#include "BaseContext.h"
#include "Interface/IExecutor.h"

#include <memory>
#include <string>

namespace Zeri::Engines::Defaults {

    class PythonContext : public BaseContext {
    public:
        PythonContext();

        [[nodiscard]] std::string GetName() const override { return "python"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::python>"; }

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
PythonContext.h
Contesto REPL Python dedicato con prompt `zeri::python>` e comandi CRUD/editoriali.
Mantiene un `PythonExecutor` condiviso costruito dal runtime rilevato via SystemGuard
ed espone interfaccia coerente al pattern già usato per il contesto Lua.
*/
