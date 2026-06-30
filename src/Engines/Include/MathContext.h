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

    private:
        [[nodiscard]] static ExecutionOutcome HandleHelp();
        [[nodiscard]] static ExecutionOutcome HandleLogic(const Command& cmd);
        [[nodiscard]] static ExecutionOutcome HandleExpression(const std::string& expr, Zeri::Core::RuntimeState& state);
        [[nodiscard]] ExecutionOutcome HandleDefineFunction(const Command& cmd, Zeri::Core::RuntimeState& state);
        [[nodiscard]] static ExecutionOutcome HandleListVariables(Zeri::Core::RuntimeState& state);
        [[nodiscard]] ExecutionOutcome HandleListFunctions(Zeri::Core::RuntimeState& state) const;
        [[nodiscard]] static ExecutionOutcome HandlePromote(const Command& cmd, Zeri::Core::RuntimeState& state);
    };

}

/*
MathContext.h — Full-featured math computation engine.

Responsibilities:
  - Free-form expression evaluation via exprtk.
  - Variable assignment, user-defined functions, scope promotion.
  - /logic command with prefix and infix forms.
  - Acts as a Julia-style sandbox for numerical computation within the REPL.

Changes:
  - HandleDefineFunction and HandleListFunctions are instance methods so the
    context can project persisted user-defined math function metadata.

Dependencies: BaseContext, RuntimeState (via ExpressionExecutor).
*/
