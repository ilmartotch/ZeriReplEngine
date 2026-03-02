#pragma once

#include "BaseContext.h"

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
    };

}
