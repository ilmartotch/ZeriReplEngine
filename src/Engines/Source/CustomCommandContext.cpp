#include "../Include/CustomCommandContext.h"

#include "../../Core/Include/RuntimeState.h"

#include <algorithm>
#include <any>
#include <sstream>

namespace {

    [[nodiscard]] std::optional<std::string> AnyToString(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value.type() == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<std::string> ParseRegistry(const std::string& raw) {
        std::vector<std::string> names;
        std::istringstream stream(raw);
        std::string line;
        while (std::getline(stream, line)) {
            if (!line.empty()) {
                names.push_back(line);
            }
        }
        return names;
    }

    [[nodiscard]] std::string SerializeRegistry(const std::vector<std::string>& names) {
        std::string raw;
        for (size_t i = 0; i < names.size(); ++i) {
            if (i > 0) {
                raw.push_back('\n');
            }
            raw.append(names[i]);
        }
        return raw;
    }

    [[nodiscard]] std::string JoinTail(const std::vector<std::string>& values, size_t from) {
        std::string joined;
        for (size_t i = from; i < values.size(); ++i) {
            if (i > from) {
                joined.push_back(' ');
            }
            joined.append(values[i]);
        }
        return joined;
    }

}

namespace Zeri::Engines::Defaults {

    std::string CustomCommandContext::BuildCommandKey(const std::string& name) {
        return "custom::commands::" + name;
    }

    std::string CustomCommandContext::RegistryKey() {
        return "custom::commands::__registry__";
    }

    std::vector<std::string> CustomCommandContext::ReadRegistry(Zeri::Core::RuntimeState& state) {
        const auto registryAny = state.GetPersistedVariable(RegistryKey());
        const auto registryRaw = AnyToString(registryAny);
        if (!registryRaw.has_value()) {
            return {};
        }
        return ParseRegistry(*registryRaw);
    }

    void CustomCommandContext::WriteRegistry(Zeri::Core::RuntimeState& state, const std::vector<std::string>& names) {
        state.SetPersistedVariable(RegistryKey(), SerializeRegistry(names));
    }

    void CustomCommandContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("CustomCommand context active. Use /define, /list, /run, /show, /delete.");
    }

    ExecutionOutcome CustomCommandContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (cmd.commandName == "define") {
            if (cmd.args.size() < 2 || cmd.args[0].empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_DEFINE_MISSING_ARGS",
                    "Missing name or body for /define.",
                    cmd.rawInput,
                    { "Usage: /define <name> \"<body>\"" }
                });
            }

            const std::string name = cmd.args[0];
            const std::string body = JoinTail(cmd.args, 1);
            if (body.empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_DEFINE_EMPTY_BODY",
                    "Command body cannot be empty.",
                    cmd.rawInput,
                    { "Usage: /define <name> \"<body>\"" }
                });
            }

            state.SetPersistedVariable(BuildCommandKey(name), body);
            auto names = ReadRegistry(state);
            if (std::find(names.begin(), names.end(), name) == names.end()) {
                names.push_back(name);
                WriteRegistry(state, names);
            }
            return "Custom command defined: " + name;
        }

        if (cmd.commandName == "list") {
            const auto names = ReadRegistry(state);
            if (names.empty()) {
                return "No custom commands defined.";
            }

            std::string output = "Custom commands:\n";
            for (const auto& name : names) {
                output += " - " + name + "\n";
            }
            return output;
        }

        if (cmd.commandName == "run") {
            if (cmd.args.empty() || cmd.args[0].empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_RUN_MISSING_NAME",
                    "Missing command name for /run.",
                    cmd.rawInput,
                    { "Usage: /run <name>" }
                });
            }

            const std::string key = BuildCommandKey(cmd.args[0]);
            const auto body = AnyToString(state.GetPersistedVariable(key));
            if (!body.has_value() || body->empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_NOT_FOUND",
                    "Custom command not found: " + cmd.args[0],
                    cmd.rawInput
                });
            }

            terminal.Write(*body);
            return *body;
        }

        if (cmd.commandName == "show") {
            if (cmd.args.empty() || cmd.args[0].empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_SHOW_MISSING_NAME",
                    "Missing command name for /show.",
                    cmd.rawInput,
                    { "Usage: /show <name>" }
                });
            }

            const auto body = AnyToString(state.GetPersistedVariable(BuildCommandKey(cmd.args[0])));
            if (!body.has_value() || body->empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_NOT_FOUND",
                    "Custom command not found: " + cmd.args[0],
                    cmd.rawInput
                });
            }
            return *body;
        }

        if (cmd.commandName == "delete") {
            if (cmd.args.empty() || cmd.args[0].empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_DELETE_MISSING_NAME",
                    "Missing command name for /delete.",
                    cmd.rawInput,
                    { "Usage: /delete <name>" }
                });
            }

            const std::string name = cmd.args[0];
            const std::string key = BuildCommandKey(name);
            const auto existing = AnyToString(state.GetPersistedVariable(key));
            if (!existing.has_value() || existing->empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_NOT_FOUND",
                    "Custom command not found: " + name,
                    cmd.rawInput
                });
            }

            state.SetPersistedVariable(key, std::string{});
            auto names = ReadRegistry(state);
            names.erase(std::remove(names.begin(), names.end(), name), names.end());
            WriteRegistry(state, names);
            return "Custom command deleted: " + name;
        }

        if (cmd.commandName == "help") {
            return "CustomCommand commands:\n"
                   "  /define <n> \"<body>\"\n"
                   "  /list\n"
                   "  /run <n>\n"
                   "  /show <n>\n"
                   "  /delete <n>";
        }

        return std::unexpected(ExecutionError{
            "SCRIPTHUB_UNKNOWN_LANG",
            "Unknown custom command operation: " + cmd.commandName,
            cmd.rawInput,
            { "Use /help to list supported commands." }
        });
    }

}

/*
CustomCommandContext.cpp
Implementa la gestione comandi utente custom su PersistedScope con namespace
`custom::commands::*`. /define salva body, /run fa echo via terminal.Write,
/list mostra i nomi registrati, /show visualizza il body, /delete rimuove dal
registro logico. Il contesto resta isolato (`$customCommand`) come vincolo v1.
*/
