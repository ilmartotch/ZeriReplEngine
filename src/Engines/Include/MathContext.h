#pragma once

#include "BaseContext.h"

namespace Zeri::Engines::Defaults {

    /**
     * @brief Full-featured math computation engine.
     *
     * Supports free-form expression evaluation via exprtk, variable assignment,
     * user-defined functions, variable promotion across scopes, and backward-
     * compatible /calc and /logic commands. Acts as a Julia-style sandbox for
     * numerical computation within the REPL.
     */
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

    private:
        [[nodiscard]] static ExecutionOutcome HandleHelp();
        [[nodiscard]] static ExecutionOutcome HandleCalc(const Command& cmd);
        [[nodiscard]] static ExecutionOutcome HandleLogic(const Command& cmd);
        [[nodiscard]] static ExecutionOutcome HandleExpression(const std::string& expr, Zeri::Core::RuntimeState& state);
        [[nodiscard]] static ExecutionOutcome HandleDefineFunction(const Command& cmd, Zeri::Core::RuntimeState& state);
        [[nodiscard]] static ExecutionOutcome HandleListVariables(Zeri::Core::RuntimeState& state);
        [[nodiscard]] static ExecutionOutcome HandleListFunctions(Zeri::Core::RuntimeState& state);
        [[nodiscard]] static ExecutionOutcome HandlePromote(const Command& cmd, Zeri::Core::RuntimeState& state);
    };

}
