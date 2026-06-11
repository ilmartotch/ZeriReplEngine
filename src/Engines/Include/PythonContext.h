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
PythonContext.h
Dedicated Python REPL context with `zeri::python>` prompt and CRUD/editor commands.
Keeps a shared `PythonExecutor` built from runtime detection via SystemGuard
and exposes an interface aligned with the Lua context pattern.
*/
