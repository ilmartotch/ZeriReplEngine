#pragma once

#include "BaseContext.h"
#include "BuiltinExecutor.h"
#include "LuaExecutor.h"
#include <memory>

namespace Zeri::Engines::Defaults {

    class GlobalContext : public BaseContext {
    public:
        void OnEnter(Zeri::Ui::ITerminal& terminal) override;
        [[nodiscard]] std::string GetName() const override { return "global"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri"; }

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

    private:
        BuiltinExecutor m_builtinExecutor;
        std::unique_ptr<LuaExecutor> m_luaExecutor;
    };

}
