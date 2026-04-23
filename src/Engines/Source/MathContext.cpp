#include "../Include/MathContext.h"
#include "../Include/ExpressionExecutor.h"
#include "../../Core/Include/RuntimeState.h"
#include <any>
#include <format>
#include <sstream>
#include <string_view>

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4702)
#endif
#include <exprtk.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace {

    [[nodiscard]] bool TryParseDouble(const std::string& value, double& out) {
        try {
            size_t read = 0;
            out = std::stod(value, &read);
            return read == value.size();
        } catch (...) {
            return false;
        }
    }

    [[nodiscard]] std::optional<bool> ParseBool(const std::string& value) {
        if (value == "true" || value == "1") return true;
        if (value == "false" || value == "0") return false;
        return std::nullopt;
    }

    [[nodiscard]] std::string_view TrimSV(std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return value;
    }

    [[nodiscard]] std::string JoinArgs(const std::vector<std::string>& args) {
        std::string result;
        for (const auto& arg : args) {
            if (!result.empty()) result += ' ';
            result += arg;
        }
        return result;
    }

    [[nodiscard]] std::optional<double> TryConvertToDouble(const std::any& value) {
        if (!value.has_value()) return std::nullopt;
        const auto& t = value.type();
        if (t == typeid(double))            return std::any_cast<double>(value);
        if (t == typeid(float))             return static_cast<double>(std::any_cast<float>(value));
        if (t == typeid(int))               return static_cast<double>(std::any_cast<int>(value));
        if (t == typeid(long))              return static_cast<double>(std::any_cast<long>(value));
        if (t == typeid(long long))         return static_cast<double>(std::any_cast<long long>(value));
        if (t == typeid(unsigned int))      return static_cast<double>(std::any_cast<unsigned int>(value));
        if (t == typeid(unsigned long))     return static_cast<double>(std::any_cast<unsigned long>(value));
        if (t == typeid(unsigned long long))return static_cast<double>(std::any_cast<unsigned long long>(value));
        if (t == typeid(std::string)) {
            double d = 0.0;
            if (TryParseDouble(std::any_cast<const std::string&>(value), d)) return d;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::string FormatDouble(double value) {
        std::ostringstream stream;
        stream << std::setprecision(15) << value;
        std::string text = stream.str();
        if (text.find('.') != std::string::npos) {
            while (!text.empty() && text.back() == '0') text.pop_back();
            if (!text.empty() && text.back() == '.') text.pop_back();
        }
        return text.empty() ? "0" : text;
    }

    struct CompiledUserFunction {
        std::string name;
        std::vector<std::string> paramNames;
        std::string body;
        exprtk::symbol_table<double> symbolTable;
        exprtk::expression<double> expression;
        std::vector<double> paramStorage;
    };

    [[nodiscard]] std::size_t FindAssignmentOperator(std::string_view text) {
        for (std::size_t i = 0; i < text.size(); ++i) {
            if (text[i] != '=') continue;
            if (i + 1 < text.size() && text[i + 1] == '=') { ++i; continue; }
            if (i > 0 && (text[i - 1] == '<' || text[i - 1] == '>' || text[i - 1] == '!')) continue;
            return i;
        }
        return std::string_view::npos;
    }
}

namespace Zeri::Engines::Defaults {

    void MathContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("Math Engine activated — type expressions directly or /help for commands.");
    }

    ExecutionOutcome MathContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)terminal;

        if (cmd.type == InputType::Expression) {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "EmptyExpression",
                    "No expression provided."
                });
            }
            return HandleExpression(cmd.args[0], state);
        }

        if (cmd.commandName == "help")    return HandleHelp();
        if (cmd.commandName == "calc")    return HandleCalc(cmd);
        if (cmd.commandName == "logic")   return HandleLogic(cmd);

        if (cmd.commandName == "eval") {
            return HandleExpression(JoinArgs(cmd.args), state);
        }

        if (cmd.commandName == "fn" || cmd.commandName == "define") {
            return HandleDefineFunction(cmd, state);
        }

        if (cmd.commandName == "vars")    return HandleListVariables(state);
        if (cmd.commandName == "fns")     return HandleListFunctions(state);
        if (cmd.commandName == "promote") return HandlePromote(cmd, state);

        return std::unexpected(ExecutionError{
            "MathErr",
            "Unknown math command: " + cmd.commandName,
            cmd.commandName,
            { "Type /help for available commands.",
              "Or type an expression directly (e.g. 2+3*sin(pi))." }
        });
    }

    ExecutionOutcome MathContext::HandleExpression(
        const std::string& expr,
        Zeri::Core::RuntimeState& state
    ) {
        return ExpressionExecutor::Evaluate(expr, state);
    }

    ExecutionOutcome MathContext::HandleDefineFunction(
        const Command& cmd,
        Zeri::Core::RuntimeState& state
    ) {
        if (cmd.args.empty()) {
            return std::unexpected(ExecutionError{
                "FnMissingArgs",
                "Missing function definition.",
                "/fn",
                { "Usage: /fn f(x) = x*sin(x)",
                  "Usage: /fn g(x, y) = x^2 + y^2" }
            });
        }

        const std::string fullDef = JoinArgs(cmd.args);

        const std::size_t eqPos = FindAssignmentOperator(fullDef);
        if (eqPos == std::string_view::npos) {
            return std::unexpected(ExecutionError{
                "FnInvalidSyntax",
                "Missing '=' in function definition.",
                fullDef,
                { "Usage: /fn f(x) = x*sin(x)" }
            });
        }

        const std::string signature(TrimSV(std::string_view(fullDef).substr(0, eqPos)));
        const std::string body(TrimSV(std::string_view(fullDef).substr(eqPos + 1)));

        if (signature.empty() || body.empty()) {
            return std::unexpected(ExecutionError{
                "FnInvalidSyntax",
                "Both function signature and body are required.",
                fullDef,
                { "Usage: /fn f(x) = x*sin(x)" }
            });
        }

        const auto parenOpen = signature.find('(');
        const auto parenClose = signature.rfind(')');
        if (parenOpen == std::string::npos || parenClose == std::string::npos || parenClose <= parenOpen) {
            return std::unexpected(ExecutionError{
                "FnInvalidSignature",
                "Invalid function signature.",
                signature,
                { "Expected: name(param1, param2, ...)" }
            });
        }

        const std::string fnName(TrimSV(std::string_view(signature).substr(0, parenOpen)));
        const std::string paramsStr(TrimSV(
            std::string_view(signature).substr(parenOpen + 1, parenClose - parenOpen - 1)
        ));

        if (fnName.empty()) {
            return std::unexpected(ExecutionError{
                "FnMissingName", "Function name is required.", signature
            });
        }

        std::vector<std::string> params;
        if (!paramsStr.empty()) {
            std::istringstream pss(paramsStr);
            std::string param;
            while (std::getline(pss, param, ',')) {
                std::string trimmed(TrimSV(param));
                if (trimmed.empty()) {
                    return std::unexpected(ExecutionError{
                        "FnInvalidParam",
                        "Empty parameter name in function definition.",
                        signature
                    });
                }
                params.push_back(std::move(trimmed));
            }
        }

        auto compiled = std::make_shared<CompiledUserFunction>();
        compiled->name = fnName;
        compiled->paramNames = params;
        compiled->body = body;
        compiled->paramStorage.resize(params.size(), 0.0);

        compiled->symbolTable.add_constants();
        compiled->symbolTable.add_constant("euler", 2.718281828459045);
        compiled->symbolTable.add_constant("phi",   1.618033988749895);
        compiled->symbolTable.add_constant("tau",   6.283185307179586);
        compiled->symbolTable.add_constant("sqrt2", 1.4142135623730951);

        for (std::size_t i = 0; i < params.size(); ++i) {
            compiled->symbolTable.add_variable(params[i], compiled->paramStorage[i]);
        }
        compiled->expression.register_symbol_table(compiled->symbolTable);

        exprtk::parser<double> exprParser;
        if (!exprParser.compile(body, compiled->expression)) {
            std::vector<std::string> hints;
            for (std::size_t i = 0; i < exprParser.error_count(); ++i) {
                hints.emplace_back(exprParser.get_error(i).diagnostic);
            }
            return std::unexpected(ExecutionError{
                "FnCompileError",
                "Failed to compile function body.",
                body,
                std::move(hints)
            });
        }

        const auto paramCount = params.size();
        Zeri::Core::RuntimeState::FunctionSignature fn =
            [compiled, paramCount](const std::vector<double>& args) -> double {
                if (args.size() < paramCount) return 0.0;
                for (std::size_t i = 0; i < paramCount; ++i) {
                    compiled->paramStorage[i] = args[i];
                }
                return compiled->expression.value();
            };

        state.SetFunction(
            Zeri::Core::RuntimeState::VariableScope::Local,
            fnName,
            std::move(fn)
        );

        std::string paramList;
        for (std::size_t i = 0; i < params.size(); ++i) {
            if (i > 0) paramList += ", ";
            paramList += params[i];
        }
        m_functionDefinitions[fnName] = std::format("{}({}) → {}", fnName, paramList, body);
        return std::format("[FunctionDefined] {}({}) = {}", fnName, paramList, body);
    }

    ExecutionOutcome MathContext::HandleListVariables(Zeri::Core::RuntimeState& state) {
        const auto localVars = state.GetCurrentLocalVariables();
        if (localVars.empty()) {
            return "No variables defined in the current scope.\n"
                   "  Assign with:  x = 42  or  result = sin(pi/4)";
        }

        std::string result = "Variables (current scope):\n";
        for (const auto& [name, value] : localVars) {
            auto numericVal = TryConvertToDouble(value);
            if (numericVal.has_value()) {
                result += std::format("  {} = {}\n", name, FormatDouble(*numericVal));
            } else {
                result += std::format("  {} = <non-numeric>\n", name);
            }
        }
        return result;
    }

    ExecutionOutcome MathContext::HandleListFunctions(Zeri::Core::RuntimeState& state) const {
        const auto localFns = state.GetCurrentLocalFunctions();
        const auto allFns = state.GetResolvedFunctions();

        std::string result;

        if (!m_functionDefinitions.empty()) {
            const std::size_t count = m_functionDefinitions.size();
            result += std::format("Defined functions ({}):\n", count);
            for (const auto& [name, definition] : m_functionDefinitions) {
                result += std::format("  {}\n", definition);
            }
        }

        bool hasInherited = false;
        for (const auto& [name, _] : allFns) {
            if (!localFns.contains(name) && !m_functionDefinitions.contains(name)) {
                if (!hasInherited) {
                    result += "Inherited functions (session/global):\n";
                    hasInherited = true;
                }
                result += std::format("  {}\n", name);
            }
        }

        if (result.empty()) {
            return "No functions defined. Use /fn f(x)=<expr> to define one.";
        }

        return result;
    }

    ExecutionOutcome MathContext::HandlePromote(
        const Command& cmd,
        Zeri::Core::RuntimeState& state
    ) {
        if (cmd.args.size() < 2) {
            return std::unexpected(ExecutionError{
                "PromoteArgs",
                "Missing arguments for /promote.",
                "/promote",
                { "Usage: /promote <varname> <session|global|persisted>" }
            });
        }

        const auto& varName = cmd.args[0];
        const auto& scopeName = cmd.args[1];

        Zeri::Core::RuntimeState::VariableScope targetScope;
        if (scopeName == "session")        targetScope = Zeri::Core::RuntimeState::VariableScope::Session;
        else if (scopeName == "global")    targetScope = Zeri::Core::RuntimeState::VariableScope::Global;
        else if (scopeName == "persisted") targetScope = Zeri::Core::RuntimeState::VariableScope::Persisted;
        else {
            return std::unexpected(ExecutionError{
                "PromoteScope",
                "Unknown scope: " + scopeName,
                scopeName,
                { "Valid scopes: session, global, persisted" }
            });
        }

        if (!state.PromoteVariable(varName, targetScope)) {
            return std::unexpected(ExecutionError{
                "PromoteFailed",
                "Could not promote variable: " + varName,
                varName,
                { "Variable must exist in local scope." }
            });
        }

        return std::format("[Promoted] {} -> {}", varName, scopeName);
    }



    ExecutionOutcome MathContext::HandleHelp() {
        return
            "Math Context — Available Commands\n"
            "\n"
            "Global Commands:\n"
            "  /help                    — Show help for the active context\n"
            "  /context                 — List available contexts\n"
            "  /back                    — Return to previous context\n"
            "  /save                    — Save session state to disk\n"
            "  /exit                    — Exit the REPL\n"
            "\n"
            "Math Commands:\n"
            "Free-form expressions (type directly, no prefix needed):\n"
            "  2 + 3 * sin(pi/4)        — Arithmetic with trig functions\n"
            "  x = 42                    — Variable assignment\n"
            "  result = x^2 + log(x)     — Computed assignment\n"
            "  f(3.14)                   — Call a user-defined function\n"
            "\n"
            "Commands:\n"
            "  /eval <expr>             — Explicit expression evaluation\n"
            "  /fn <sig> = <body>       — Define function (e.g. /fn f(x) = x*sin(x))\n"
            "  /define                  — Alias for /fn\n"
            "  /vars                    — List variables in current scope\n"
            "  /fns                     — List defined and available functions\n"
            "  /promote <var> <scope>   — Promote variable (session|global|persisted)\n"
            "  /calc <a> <op> <b>       — Legacy arithmetic (e.g. /calc 2 * 8)\n"
            "  /logic <op> <v1> <v2>    — Boolean logic (and|or|xor true|false)\n"
            "\n"
            "Built-in functions (exprtk):\n"
            "  Trig:    sin cos tan asin acos atan atan2\n"
            "  Hyp:     sinh cosh tanh asinh acosh atanh\n"
            "  Log:     log log2 log10 exp\n"
            "  Power:   pow sqrt abs cbrt\n"
            "  Round:   ceil floor round trunc frac\n"
            "  Other:   min max clamp mod if(cond, t, f)\n"
            "\n"
            "Constants:\n"
            "  pi = 3.14159...  euler = 2.71828...  phi = 1.61803...\n"
            "  tau = 6.28318...  sqrt2 = 1.41421...  inf  epsilon\n"
            "\n"
            "Variable scopes:\n"
            "  Default: local (lost on context exit)\n"
            "  /promote x session     — Persist for REPL session lifetime\n"
            "  /promote x persisted   — Persist to disk across sessions\n"
            "\n"
            "Pipeline:\n"
            "  $math | x = 5 | sin(x) + 1";
    }

    ExecutionOutcome MathContext::HandleCalc(const Command& cmd) {
        std::vector<std::string> effectiveArgs = cmd.args;

        if (effectiveArgs.size() < 3 && cmd.pipeInput.has_value()) {
            std::istringstream iss(*cmd.pipeInput);
            std::string a, op, b;
            if (iss >> a >> op >> b) {
                effectiveArgs = { a, op, b };
            }
        }

        if (effectiveArgs.size() < 3) {
            return std::unexpected(ExecutionError{
                "MathArgs",
                "Invalid arguments for /calc.",
                "/calc",
                { "Usage: /calc <a> <+|-|*|/> <b>",
                  "Or provide expression via pipeline.",
                  "Tip: type expressions directly instead (e.g. 2+3)." }
            });
        }

        double lhs = 0.0;
        double rhs = 0.0;
        if (!TryParseDouble(effectiveArgs[0], lhs) || !TryParseDouble(effectiveArgs[2], rhs)) {
            return std::unexpected(ExecutionError{
                "MathNumber",
                "Both operands must be valid numeric values.",
                "/calc",
                { "Example: /calc 10.5 * 2" }
            });
        }

        const std::string& op = effectiveArgs[1];
        double result = 0.0;

        if (op == "+")      result = lhs + rhs;
        else if (op == "-") result = lhs - rhs;
        else if (op == "*") result = lhs * rhs;
        else if (op == "/") {
            if (rhs == 0.0) {
                return std::unexpected(ExecutionError{
                    "MathDivideByZero",
                    "Division by zero is not allowed.",
                    "/calc",
                    { "Use a non-zero divisor." }
                });
            }
            result = lhs / rhs;
        } else {
            return std::unexpected(ExecutionError{
                "MathOperator",
                "Unsupported operator.",
                "/calc",
                { "Supported operators: + - * /" }
            });
        }

        return std::format("{} {} {} = {}", lhs, op, rhs, result);
    }

    ExecutionOutcome MathContext::HandleLogic(const Command& cmd) {
        if (cmd.args.size() < 3) {
            return std::unexpected(ExecutionError{
                "LogicArgs",
                "Invalid arguments for /logic.",
                "/logic",
                { "Usage: /logic <and|or|xor> <true|false> <true|false>" }
            });
        }

        auto lhs = ParseBool(cmd.args[1]);
        auto rhs = ParseBool(cmd.args[2]);

        if (!lhs.has_value() || !rhs.has_value()) {
            return std::unexpected(ExecutionError{
                "LogicBoolean",
                "Operands must be boolean values.",
                "/logic",
                { "Accepted values: true, false, 1, 0" }
            });
        }

        const std::string& op = cmd.args[0];
        bool result = false;

        if (op == "and")      result = *lhs && *rhs;
        else if (op == "or")  result = *lhs || *rhs;
        else if (op == "xor") result = (*lhs != *rhs);
        else {
            return std::unexpected(ExecutionError{
                "LogicOperator",
                "Unsupported logical operator.",
                "/logic",
                { "Supported operators: and, or, xor" }
            });
        }

        return result ? "true" : "false";
    }

}

/*
MathContext.cpp — Mathematical expression engine.

Handles math expressions and logic operations via exprtk.
Supports explicit slash commands (/calc, /logic, /eval, /fn, /vars, /fns,
/promote), variable promotion and assignment, and user-defined functions.

Changes:
  - Added m_functionDefinitions (std::map<std::string, std::string>) to MathContext
    to store each user-defined function as a display string (signature → body).
  - HandleDefineFunction now records the definition text in m_functionDefinitions
    on successful compile and registration.
  - HandleListFunctions now uses m_functionDefinitions to render structured output:
    "f(x)  →  x * sin(x)" format, with count header and empty-state message.
  - Both HandleDefineFunction and HandleListFunctions changed from static to
    non-static member methods to access the instance map.
*/