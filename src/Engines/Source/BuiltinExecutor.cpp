#include "../Include/BuiltinExecutor.h"
#include "../Include/Interface/IContext.h"
#include "../../Core/Include/HelpCatalog.h"

#include <any>
#include <cctype>
#include <cstdlib>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

    enum class SetValueType {
        Number,
        String,
        Boolean
    };

    [[nodiscard]] std::string Trim(std::string_view text) {
        size_t start = 0;
        while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start])) != 0) {
            ++start;
        }

        size_t end = text.size();
        while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }

        return std::string(text.substr(start, end - start));
    }

    [[nodiscard]] std::string ToLower(std::string_view text) {
        std::string out;
        out.reserve(text.size());
        for (char ch : text) {
            out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return out;
    }

    [[nodiscard]] std::string JoinTail(const std::vector<std::string>& args, size_t from) {
        std::string result;
        for (size_t i = from; i < args.size(); ++i) {
            if (!result.empty()) {
                result.push_back(' ');
            }
            result.append(args[i]);
        }
        return result;
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

    [[nodiscard]] std::optional<double> ParseNumber(std::string_view text) {
        const std::string trimmed = Trim(text);
        if (trimmed.empty()) {
            return std::nullopt;
        }

        char* end = nullptr;
        const double value = std::strtod(trimmed.c_str(), &end);
        if (end == nullptr || end != trimmed.c_str() + trimmed.size()) {
            return std::nullopt;
        }
        return value;
    }

    [[nodiscard]] std::optional<bool> ParseBoolean(std::string_view text) {
        const std::string normalized = ToLower(Trim(text));
        if (normalized == "true" || normalized == "t") {
            return true;
        }
        if (normalized == "false" || normalized == "f") {
            return false;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> ExtractSetValue(const Zeri::Engines::Command& cmd) {
        if (cmd.args.size() >= 2) {
            size_t valueStart = 1;
            if (cmd.args[1] == "=") {
                valueStart = 2;
            }
            if (valueStart < cmd.args.size()) {
                return JoinTail(cmd.args, valueStart);
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] bool ResolveSetType(
        const Zeri::Engines::Command& cmd,
        SetValueType& outputType,
        Zeri::Engines::ExecutionError& error) {
        const auto isKnownTypeFlag = [](const std::string& flag) {
            return flag == "number" || flag == "string" || flag == "bool";
        };

        for (const auto& [flag, _] : cmd.flags) {
            if (!isKnownTypeFlag(flag)) {
                error = {
                    "SetUnknownFlag",
                    "Unknown /set flag: --" + flag,
                    cmd.rawInput,
                    {
                        "Valid flags: --number, --string, --bool",
                        "Example: /set myVar = 42 --number"
                    }
                };
                return false;
            }
        }

        const bool hasNumber = cmd.flags.contains("number");
        const bool hasString = cmd.flags.contains("string");
        const bool hasBool = cmd.flags.contains("bool");
        const int flagCount = static_cast<int>(hasNumber) + static_cast<int>(hasString) + static_cast<int>(hasBool);

        if (flagCount == 0) {
            error = {
                "SetTypeRequired",
                "Missing type flag for /set.",
                cmd.rawInput,
                {
                    "Retry with one type flag: --number, --string, or --bool",
                    "Examples:",
                    "/set x = 5 --number",
                    "/set home = Milan --string",
                    "/set featureEnabled = t --bool"
                }
            };
            return false;
        }

        if (flagCount > 1) {
            error = {
                "SetTypeConflict",
                "Only one type flag is allowed for /set.",
                cmd.rawInput,
                {
                    "Use exactly one: --number, --string, or --bool",
                    "Example: /set x = 5 --number"
                }
            };
            return false;
        }

        if (hasNumber) {
            outputType = SetValueType::Number;
        } else if (hasString) {
            outputType = SetValueType::String;
        } else {
            outputType = SetValueType::Boolean;
        }
        return true;
    }

    [[nodiscard]] std::optional<std::string> AnyToDisplayString(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }

        const auto& type = value.type();
        if (type == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
        if (type == typeid(double)) {
            return FormatDouble(std::any_cast<double>(value));
        }
        if (type == typeid(float)) {
            return FormatDouble(static_cast<double>(std::any_cast<float>(value)));
        }
        if (type == typeid(int)) {
            return std::to_string(std::any_cast<int>(value));
        }
        if (type == typeid(long)) {
            return std::to_string(std::any_cast<long>(value));
        }
        if (type == typeid(long long)) {
            return std::to_string(std::any_cast<long long>(value));
        }
        if (type == typeid(unsigned int)) {
            return std::to_string(std::any_cast<unsigned int>(value));
        }
        if (type == typeid(unsigned long)) {
            return std::to_string(std::any_cast<unsigned long>(value));
        }
        if (type == typeid(unsigned long long)) {
            return std::to_string(std::any_cast<unsigned long long>(value));
        }
        if (type == typeid(bool)) {
            return std::any_cast<bool>(value) ? "true" : "false";
        }

        return std::nullopt;
    }

}

namespace Zeri::Engines::Defaults {

    ExecutionOutcome BuiltinExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal&
    ) {
        if (cmd.commandName == "exit") {
            state.RequestExit();
            return "Exiting...";
        }

        if (cmd.commandName == "help") {
            std::string output;
            output += "Zeri REPL — Available Commands\n";
            output += "\n";
            output += "Syntax:\n";
            output += "/<command> Execute a command in the current context\n";
            output += "$<context> Switch context using reachable targets from /context\n";
            output += "!<shell_command> Execute a system shell command\n";
            output += "<expr> Evaluate an expression (context-dependent)\n";
            output += "# comment Inline comment (ignored by parser)\n";
            output += "\n";
            output += "Global Commands:\n";

            const auto& globalCommands = Zeri::Core::HelpCatalog::Instance().CommandsForGroup("global");
            for (const auto& command : globalCommands) {
                output += "  ";
                output += command.command;
                output += " — ";
                output += command.synopsis;
                output += "\n";
            }

            output += "\n";
            output += "Contexts:\n";
            const auto reachable = Zeri::Core::HelpCatalog::Instance().ReachableFrom("global");
            for (const auto& contextName : reachable) {
                const auto* context = Zeri::Core::HelpCatalog::Instance().FindContext(contextName);
                if (context == nullptr) {
                    continue;
                }

                output += "  $";
                output += context->name;
                output += " — ";
                output += context->description;
                output += "\n";
            }

            output += "\nType /help inside a context for context-specific commands.";
            return output;
        }

        auto scope = Zeri::Core::RuntimeState::VariableScope::Global;
        std::string scopeLabel = "global";

        if (auto* ctx = state.GetCurrentContext(); ctx && ctx->GetName() != "global") {
            scope = Zeri::Core::RuntimeState::VariableScope::Local;
            scopeLabel = "local";
        }

        if (cmd.commandName == "set") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "MissingArguments",
                    "Missing arguments for set",
                    cmd.rawInput,
                    {
                        "Usage: /set <key> [=] <value> --number|--string|--bool",
                        "Example: /set x = 5 --number"
                    }
                });
            }

            const auto value = ExtractSetValue(cmd);
            if (!value.has_value()) {
                return std::unexpected(ExecutionError{
                    "MissingArguments",
                    "Missing value for set",
                    cmd.rawInput,
                    {
                        "Usage: /set <key> [=] <value> --number|--string|--bool"
                    }
                });
            }

            SetValueType setType = SetValueType::String;
            ExecutionError typeError;
            if (!ResolveSetType(cmd, setType, typeError)) {
                return std::unexpected(typeError);
            }

            const auto key = cmd.args[0];
            const std::string trimmedValue = Trim(*value);
            switch (setType) {
            case SetValueType::Number: {
                const auto parsed = ParseNumber(trimmedValue);
                if (!parsed.has_value()) {
                    return std::unexpected(ExecutionError{
                        "InvalidSetNumber",
                        "Invalid numeric value for /set.",
                        cmd.rawInput,
                        {
                            "Use a valid numeric value with --number",
                            "Example: /set x = 5.25 --number"
                        }
                    });
                }
                state.SetVariable(scope, key, *parsed);
                return "Variable set (" + scopeLabel + ", number): " + key + " = " + FormatDouble(*parsed);
            }
            case SetValueType::Boolean: {
                const auto parsed = ParseBoolean(trimmedValue);
                if (!parsed.has_value()) {
                    return std::unexpected(ExecutionError{
                        "InvalidSetBool",
                        "Invalid boolean value for /set.",
                        cmd.rawInput,
                        {
                            "Accepted values for --bool: true, false, t, f",
                            "Example: /set isReady = true --bool"
                        }
                    });
                }
                state.SetVariable(scope, key, *parsed);
                return "Variable set (" + scopeLabel + ", bool): " + key + " = " + (*parsed ? "true" : "false");
            }
            case SetValueType::String:
            default:
                state.SetVariable(scope, key, *value);
                return "Variable set (" + scopeLabel + ", string): " + key + " = " + *value;
            }
        }

        if (cmd.commandName == "get") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "MissingArguments",
                    "Missing key for get",
                    cmd.rawInput,
                    { "Usage: /get <key>" }
                });
            }

            auto val = state.GetVariable(scope, cmd.args[0]);
            if (!val.has_value()) {
                return "Variable not found.";
            }

            const auto textValue = AnyToDisplayString(val);
            if (!textValue.has_value()) {
                return "Value has an unsupported type.";
            }

            return *textValue;
        }

        return std::unexpected(ExecutionError{
            "UnknownBuiltin",
            "Command not implemented.",
            cmd.rawInput
        });
    }

    ExecutionType BuiltinExecutor::GetType() const {
        return ExecutionType::Builtin;
    }

}

/*
BuiltinExecutor.cpp — Handles global built-in commands.

Routes:
  - /exit: Sets the exit flag in RuntimeState.
  - /help: Returns formatted help text listing all syntax forms, global
    commands (including /status, /reset, /clear), and available contexts.
  - /set: Writes a typed variable to the current scope using explicit flags.
  - /get: Reads a variable from the current scope and renders typed values.

QA Changes:
  - /help text reformatted with structured sections (Syntax, Global Commands,
    Contexts) and consistent description alignment.
  - Added /status, /reset, /clear entries to Global Commands section.
  - /status and /reset are routed in HandleGlobalCommand (main.cpp) before
    reaching BuiltinExecutor; they appear here only in help text.
*/
