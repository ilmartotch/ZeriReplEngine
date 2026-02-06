#include "../Include/ExpressionExecutor.h"
#include <algorithm>

namespace Zeri::Engines::Defaults {

    ExpressionExecutor::ExpressionExecutor() = default;

    ExecutionOutcome ExpressionExecutor::Evaluate(
        const std::string& expression,
        Zeri::Core::RuntimeState& state) {
        ExpressionExecutor executor{};
        return executor.EvaluateState(expression, state);
    }

    ExecutionOutcome ExpressionExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state
    ) {
        // Per ora gestiamo solo il caso /math -> @context_eval <expr>
        if (cmd.commandName == "@context_eval") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "MissingExpression",
                    "Nessuna espressione da valutare",
                    cmd.rawInput,
                    { "Passa l'espressione da valutare come primo argomento" }
                });
            }
            return EvaluateState(cmd.args[0], state);
        }

        return std::unexpected(ExecutionError{
            "UnsupportedCommand",
            "Comando non gestito da ExpressionExecutor",
            cmd.commandName,
            { "Verifica il dispatcher o il tipo di executor da usare" }
        });
    }

    ExecutionType ExpressionExecutor::GetType() const {
        return ExecutionType::Expression;
    }

    ExecutionOutcome ExpressionExecutor::EvaluateFunction(const FunctionCall& fc) const {
        // Stub minimale: nessuna funzione registrata
        return std::unexpected(ExecutionError{
            "FunctionNotImplemented",
            "Funzione non implementata: " + fc.name,
            fc.name,
            { "Registra la funzione in m_functions oppure implementa un parser" }
        });
    }

    ExecutionOutcome ExpressionExecutor::EvaluateState(
        const std::string& expression,
        Zeri::Core::RuntimeState& state) const {

        if (size_t pos = expression.find('='); pos != std::string::npos) {
            std::string varName = expression.substr(0, pos);
            std::string value = expression.substr(pos + 1);

            varName.erase(std::remove(varName.begin(), varName.end(), ' '), varName.end());

            state.SetVariable(varName, value);
            return "[VariableBinding] " + varName + " = " + value;
        }

        return "[MathResult] Evaluated: " + expression;
    }

}