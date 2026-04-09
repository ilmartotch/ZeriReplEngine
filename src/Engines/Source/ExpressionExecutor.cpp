#include "../Include/ExpressionExecutor.h"
#include <any>
#include <cctype>
#include <charconv>
#include <iomanip>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4702)
#endif
#include <exprtk.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace {

    class RuntimeFunctionAdapter final : public exprtk::ivararg_function<double> {
    public:
        using FunctionSignature = Zeri::Core::RuntimeState::FunctionSignature;

        explicit RuntimeFunctionAdapter(FunctionSignature function)
            : m_function(std::move(function)) {}

        double operator()(const std::vector<double>& args) override {
            return m_function(args);
        }

    private:
        FunctionSignature m_function;
    };

    struct CachedExpression {
        exprtk::symbol_table<double> symbolTable;
        exprtk::expression<double> expression;
        std::unordered_map<std::string, double> variableStorage;
        std::unordered_set<std::string> identifiers;
        std::vector<std::unique_ptr<RuntimeFunctionAdapter>> functionAdapters;
        std::size_t functionRevision{ 0 };
    };

    std::unordered_map<std::string, CachedExpression> g_expressionCache;
    std::mutex g_expressionCacheMutex;

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

    const std::unordered_set<std::string> kBuiltinNames = {
        "pi", "epsilon", "inf",
        "euler", "phi", "tau", "sqrt2",
        "true", "false"
    };

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
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal&
    ) {
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

        size_t assignmentPos = std::string::npos;
        for (size_t idx = 0; idx < trimmed.size(); ++idx) {
            if (trimmed[idx] != '=') continue;
            if (idx + 1 < trimmed.size() && trimmed[idx + 1] == '=') { ++idx; continue; }
            if (idx > 0 && (trimmed[idx - 1] == '<' || trimmed[idx - 1] == '>' || trimmed[idx - 1] == '!')) continue;
            assignmentPos = idx;
            break;
        }
        const bool isAssignment = assignmentPos != std::string::npos;
        std::string variableName;
        std::string expressionText = trimmed;

        if (isAssignment) {
            variableName = Trim(std::string_view(trimmed).substr(0, assignmentPos));
            expressionText = Trim(std::string_view(trimmed).substr(assignmentPos + 1));

            if (variableName.empty() || expressionText.empty() || !IsValidIdentifier(variableName)) {
                return std::unexpected(ExecutionError{
                    "InvalidAssignment",
                    "Invalid assignment syntax.",
                    trimmed,
                    { "Format: variable_name = expression" }
                });
            }
        }

        const auto functionRevision = state.GetFunctionRegistryRevision();

        std::unique_lock cacheLock(g_expressionCacheMutex);
        auto cacheIt = g_expressionCache.find(expressionText);

        if (cacheIt == g_expressionCache.end() || cacheIt->second.functionRevision != functionRevision) {
            CachedExpression entry;
            entry.identifiers = ExtractIdentifiers(expressionText);
            entry.functionRevision = functionRevision;
            entry.symbolTable.add_constants();
            entry.symbolTable.add_constant("euler", 2.718281828459045);
            entry.symbolTable.add_constant("phi",   1.618033988749895);
            entry.symbolTable.add_constant("tau",   6.283185307179586);
            entry.symbolTable.add_constant("sqrt2", 1.4142135623730951);

            for (const auto& identifier : entry.identifiers) {
                if (kBuiltinNames.contains(identifier)) continue;
                if (!state.HasVariable(identifier)) {
                    return std::unexpected(ExecutionError{
                        "MissingVariable",
                        "Undefined variable: " + identifier,
                        identifier,
                        { "Define the variable first (e.g. " + identifier + " = <value>)" }
                    });
                }

                const auto value = TryConvertToDouble(state.GetVariable(identifier));
                if (!value.has_value()) {
                    return std::unexpected(ExecutionError{
                        "InvalidVariableType",
                        "Non-numeric variable: " + identifier,
                        identifier,
                        { "Store a numeric value (int, float, double or numeric string)" }
                    });
                }

                auto [it, inserted] = entry.variableStorage.emplace(identifier, *value);
                entry.symbolTable.add_variable(it->first, it->second);
            }

            const auto functions = state.GetResolvedFunctions();
            entry.functionAdapters.reserve(functions.size());
            for (const auto& [name, fn] : functions) {
                auto adapter = std::make_unique<RuntimeFunctionAdapter>(fn);
                entry.symbolTable.add_function(name, *adapter);
                entry.functionAdapters.push_back(std::move(adapter));
            }

            entry.expression.register_symbol_table(entry.symbolTable);

            exprtk::parser<double> parser;
            if (!parser.compile(expressionText, entry.expression)) {
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

            cacheIt = g_expressionCache.insert_or_assign(expressionText, std::move(entry)).first;
        }

        auto& cacheEntry = cacheIt->second;
        for (const auto& identifier : cacheEntry.identifiers) {
            if (kBuiltinNames.contains(identifier)) continue;
            if (!state.HasVariable(identifier)) {
                return std::unexpected(ExecutionError{
                    "MissingVariable",
                    "Undefined variable: " + identifier,
                    identifier,
                    { "Define the variable first (e.g. " + identifier + " = <value>)" }
                });
            }

            const auto value = TryConvertToDouble(state.GetVariable(identifier));
            if (!value.has_value()) {
                return std::unexpected(ExecutionError{
                    "InvalidVariableType",
                    "Non-numeric variable: " + identifier,
                    identifier,
                    { "Store a numeric value (int, float, double or numeric string)" }
                });
            }

            cacheEntry.variableStorage[identifier] = *value;
        }

        const double result = cacheEntry.expression.value();
        cacheLock.unlock();

        if (isAssignment) {
            state.SetVariable(variableName, result);
            return "[VariableBinding] " + variableName + " = " + FormatDouble(result);
        }

        return "[MathResult] " + FormatDouble(result);
    }

}

/*
ExpressionExecutor.cpp — Mathematical expression evaluator via exprtk.

Responsabilità:
  - Parses and evaluates mathematical expressions with variable binding.
  - Supports assignment (x = expr), bare evaluation, and RuntimeState
    variable/function integration.
  - Caches compiled exprtk expressions for repeated evaluation.
  - Thread-safe via g_expressionCacheMutex.

Note:
  MSVC C4702 warning is suppressed around exprtk.hpp include because
  exprtk emits unreachable-code warnings that would be promoted to errors
  by /WX. See: https://learn.microsoft.com/en-us/cpp/error-messages/compiler-warnings/compiler-warning-level-4-c4702

Dipendenze: ExpressionExecutor.h, exprtk, RuntimeState.
*/