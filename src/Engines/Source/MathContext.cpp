#include "../Include/MathContext.h"
#include "../../Core/Include/RuntimeState.h"
#include <format>
#include <sstream>
#include <any>

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
}

namespace Zeri::Engines::Defaults {

    void MathContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteLine("--- Math Engine Activated ---");
        terminal.WriteLine("Type /help to see math commands.");
    }

    ExecutionOutcome MathContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)state;
        (void)terminal;

        if (cmd.commandName == "help") {
            return
                "Math Context Help\n"
                "-----------------\n"
                "Commands\n"
                "  /calc <a> <+|-|*|/> <b>\n"
                "  /logic <and|or|xor> <true|false> <true|false>\n"
                "\n"
                "Pipeline integration\n"
                "  - If /calc is called with missing args, it tries __pipe_value\n"
                "    as expression text: \"a <op> b\"\n"
                "\n"
                "Examples\n"
                "  /calc 2 * 8\n"
                "  /logic xor true false\n"
                "  /set expr \"10 + 5\" | /calc\n";
        }

        if (cmd.commandName == "calc") {
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
                    { "Usage: /calc <a> <+|-|*|/> <b>", "Or provide expression through __pipe_value." }
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

            if (op == "+") result = lhs + rhs;
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

        if (cmd.commandName == "logic") {
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

            if (op == "and") result = *lhs && *rhs;
            else if (op == "or") result = *lhs || *rhs;
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

        return std::unexpected(ExecutionError{
            "MathErr",
            "Unknown math command.",
            cmd.commandName,
            { "Try /help, /calc or /logic." }
        });
    }

}