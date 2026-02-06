#pragma once
#include "Interface/IExecutor.h"
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace Zeri::Engines::Defaults {

    class ExpressionExecutor : public IExecutor {
    public:
        ExpressionExecutor();

        // Valutazione rapida senza passare dal dispatcher
        [[nodiscard]] static ExecutionOutcome Evaluate(
            const std::string& expression,
            Zeri::Core::RuntimeState& state);

        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state
        ) override;

        [[nodiscard]] ExecutionType GetType() const override;

    private:
        std::map<std::string, std::function<double(std::vector<double>)>> m_functions;

        [[nodiscard]] ExecutionOutcome EvaluateFunction(const FunctionCall& fc) const;
        [[nodiscard]] ExecutionOutcome EvaluateState(const std::string& expression, Zeri::Core::RuntimeState& state) const;
    };

}

/*
Header for `ExpressionExecutor`.
Handles mathematical expression evaluation as per spec: "Expressions and Evaluation".
Supports nested function invocation, function Invocation and Nesting.
*/