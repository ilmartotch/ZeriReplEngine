#pragma once

#include "BaseContext.h"

namespace Zeri::Engines::Defaults {

    class MathContext : public BaseContext {
    public:
        void OnEnter(Zeri::Ui::ITerminal& terminal) override;
        [[nodiscard]] std::string GetName() const override { return "math"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::math"; }

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;
    };

}
