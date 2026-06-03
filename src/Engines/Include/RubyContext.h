#pragma once

#include "BaseContext.h"
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
RubyContext.h
Dedicated Ruby REPL context with `zeri::ruby>` prompt and editor/CRUD workflow
aligned with existing script contexts (Lua/Python/JS), with a shared executor
built from runtime detection via SystemGuard.
*/
