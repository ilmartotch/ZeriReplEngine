#pragma once

#include "BaseContext.h"
#include "BuiltinExecutor.h"
#include "Interface/IExecutor.h"

#include <memory>
#include <string>

namespace Zeri::Engines::Defaults {

    class RubyContext : public BaseContext {
    public:
        RubyContext();

        [[nodiscard]] std::string GetName() const override { return "ruby"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::ruby>"; }

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
        BuiltinExecutor m_builtinExecutor;
        bool m_initialized{ false };
    };

}

/*
RubyContext.h
Dedicated Ruby REPL context with `zeri::ruby>` prompt and editor/CRUD workflow
aligned with existing script contexts (Lua/Python/JS), with a shared executor
built from runtime detection via SystemGuard.
*/
