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
        std::map<std::string, std::string> m_functionDefinitions;

        [[nodiscard]] static ExecutionOutcome HandleHelp();
        [[nodiscard]] static ExecutionOutcome HandleCalc(const Command& cmd);
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
  - Backward-compatible /calc and /logic commands.
  - Acts as a Julia-style sandbox for numerical computation within the REPL.

Changes:
  - Added m_functionDefinitions (std::map<std::string, std::string>) instance
    member to store function display text (name(params) → body) at define time.
  - HandleDefineFunction and HandleListFunctions changed from static to non-static
    to allow access to the instance map.

Dependencies: BaseContext, RuntimeState (via ExpressionExecutor).
*/
