#include "../Include/ExpressionExecutor.h"
#include <any>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <exprtk.hpp>

namespace {

    [[nodiscard]] std::string Trim(std::string_view text) {
        size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
            ++start;
        }

        size_t end = text.size();
        while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
            --end;
        }

        return std::string(text.substr(start, end - start));
    }

    [[nodiscard]] bool IsValidIdentifier(std::string_view text) {
        if (text.empty()) {
            return false;
        }

        const auto first = static_cast<unsigned char>(text.front());
        if (!(std::isalpha(first) || text.front() == '_')) {
            return false;
        }

        for (size_t i = 1; i < text.size(); ++i) {
            const auto ch = static_cast<unsigned char>(text[i]);
            if (!(std::isalnum(ch) || text[i] == '_')) {
                return false;
            }
        }

        return true;
    }

    [[nodiscard]] std::optional<double> TryConvertToDouble(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }

        if (value.type() == typeid(double)) {
            return std::any_cast<double>(value);
        }
        if (value.type() == typeid(float)) {
            return static_cast<double>(std::any_cast<float>(value));
        }
        if (value.type() == typeid(int)) {
            return static_cast<double>(std::any_cast<int>(value));
        }
        if (value.type() == typeid(long)) {
            return static_cast<double>(std::any_cast<long>(value));
        }
        if (value.type() == typeid(long long)) {
            return static_cast<double>(std::any_cast<long long>(value));
        }
        if (value.type() == typeid(unsigned int)) {
            return static_cast<double>(std::any_cast<unsigned int>(value));
        }
        if (value.type() == typeid(unsigned long)) {
            return static_cast<double>(std::any_cast<unsigned long>(value));
        }
        if (value.type() == typeid(unsigned long long)) {
            return static_cast<double>(std::any_cast<unsigned long long>(value));
        }
        if (value.type() == typeid(std::string)) {
            const auto& text = std::any_cast<const std::string&>(value);
            double parsed = 0.0;
            const auto* begin = text.data();
            const auto* end = text.data() + text.size();
            auto [ptr, ec] = std::from_chars(begin, end, parsed);
            if (ec == std::errc() && ptr == end) {
                return parsed;
            }
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string FormatDouble(double value) {
        std::ostringstream stream;
        stream << std::setprecision(15) << value;
        std::string text = stream.str();

        if (text.find('.') != std::string::npos) {
            while (!text.empty() && text.back() == '0') {
                text.pop_back();
            }
            if (!text.empty() && text.back() == '.') {
                text.pop_back();
            }
        }

        return text.empty() ? "0" : text;
    }

    [[nodiscard]] std::unordered_set<std::string> ExtractIdentifiers(std::string_view expression) {
        std::unordered_set<std::string> identifiers;

        for (size_t i = 0; i < expression.size();) {
            const char current = expression[i];

            if ((current == 'e' || current == 'E') && i > 0 &&
                std::isdigit(static_cast<unsigned char>(expression[i - 1]))) {
                ++i;
                continue;
            }

            if (std::isalpha(static_cast<unsigned char>(current)) || current == '_') {
                const size_t start = i++;
                while (i < expression.size()) {
                    const auto ch = static_cast<unsigned char>(expression[i]);
                    if (!(std::isalnum(ch) || expression[i] == '_')) {
                        break;
                    }
                    ++i;
                }

                std::string name(expression.substr(start, i - start));

                size_t next = i;
                while (next < expression.size() &&
                    std::isspace(static_cast<unsigned char>(expression[next]))) {
                    ++next;
                }

                if (next < expression.size() && expression[next] == '(') {
                    continue;
                }

                identifiers.emplace(std::move(name));
                continue;
            }

            ++i;
        }

        return identifiers;
    }

}

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

        const std::string trimmed = Trim(expression);
        if (trimmed.empty()) {
            return std::unexpected(ExecutionError{
                "EmptyExpression",
                "Espressione vuota",
                expression,
                { "Inserisci un'espressione matematica valida" }
            });
        }

        const size_t assignmentPos = trimmed.find('=');
        const bool isAssignment = assignmentPos != std::string::npos;
        std::string variableName;
        std::string expressionText = trimmed;

        if (isAssignment) {
            variableName = Trim(std::string_view(trimmed).substr(0, assignmentPos));
            expressionText = Trim(std::string_view(trimmed).substr(assignmentPos + 1));

            if (variableName.empty() || expressionText.empty() || !IsValidIdentifier(variableName)) {
                return std::unexpected(ExecutionError{
                    "InvalidAssignment",
                    "Assegnazione non valida",
                    trimmed,
                    { "Usa il formato: nome_variabile = espressione" }
                });
            }
        }

        std::unordered_map<std::string, double> variableStorage;
        exprtk::symbol_table<double> symbolTable;
        symbolTable.add_constants();

        for (const auto& identifier : ExtractIdentifiers(expressionText)) {
            if (!state.HasVariable(identifier)) {
                continue;
            }

            const auto value = TryConvertToDouble(state.GetVariable(identifier));
            if (!value.has_value()) {
                return std::unexpected(ExecutionError{
                    "InvalidVariableType",
                    "Variabile non numerica: " + identifier,
                    identifier,
                    { "Salva un valore numerico (int, float, double o stringa numerica)" }
                });
            }

            auto [it, inserted] = variableStorage.emplace(identifier, *value);
            symbolTable.add_variable(it->first, it->second);
        }

        exprtk::expression<double> compiledExpression;
        compiledExpression.register_symbol_table(symbolTable);

        exprtk::parser<double> parser;
        if (!parser.compile(expressionText, compiledExpression)) {
            std::vector<std::string> hints;
            for (std::size_t i = 0; i < parser.error_count(); ++i) {
                const auto error = parser.get_error(i);
                hints.emplace_back(error.diagnostic);
            }

            return std::unexpected(ExecutionError{
                "ExpressionParseError",
                "Errore di compilazione ExprTk",
                expressionText,
                std::move(hints)
            });
        }

        const double result = compiledExpression.value();

        if (isAssignment) {
            state.SetVariable(variableName, result);
            return "[VariableBinding] " + variableName + " = " + FormatDouble(result);
        }

        return "[MathResult] " + FormatDouble(result);
    }

}