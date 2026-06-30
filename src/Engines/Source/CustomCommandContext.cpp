#include "../Include/CustomCommandContext.h"

#include "../../Core/Include/HelpCatalog.h"
#include "../../Core/Include/RuntimeState.h"
#include "../../Core/Include/StringUtils.h"

#include <algorithm>
#include <any>
#include <cstdint>
#include <cstdlib>
#include <map>
#include <ranges>
#include <set>
#include <sstream>

namespace {
    constexpr std::int64_t kCustomCommandSchemaVersion = 2;
    constexpr std::string_view kGlobalScope = "global";
    constexpr std::string_view kContextPrefix = "context:";

    [[nodiscard]] std::optional<std::string> AnyToString(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value.type() == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::int64_t> AnyToInt64(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value.type() == typeid(std::int64_t)) {
            return std::any_cast<std::int64_t>(value);
        }
        if (value.type() == typeid(int)) {
            return static_cast<std::int64_t>(std::any_cast<int>(value));
        }
        if (value.type() == typeid(long)) {
            return static_cast<std::int64_t>(std::any_cast<long>(value));
        }
        if (value.type() == typeid(long long)) {
            return static_cast<std::int64_t>(std::any_cast<long long>(value));
        }
        if (value.type() == typeid(std::string)) {
            const auto raw = std::any_cast<std::string>(value);
            if (raw.empty()) {
                return std::nullopt;
            }
            char* end = nullptr;
            const auto parsed = std::strtoll(raw.c_str(), &end, 10);
            if (end == raw.c_str() + static_cast<std::ptrdiff_t>(raw.size())) {
                return static_cast<std::int64_t>(parsed);
            }
            return std::nullopt;
        }
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::map<std::string, std::any>> AnyToMap(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value.type() == typeid(std::map<std::string, std::any>)) {
            return std::any_cast<std::map<std::string, std::any>>(value);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<std::string> ParseRegistry(const std::string& raw) {
        std::vector<std::string> entries;
        std::istringstream stream(raw);
        std::string line;
        while (std::getline(stream, line)) {
            const std::string trimmed = Zeri::Core::Utils::Trim(line);
            if (!trimmed.empty()) {
                entries.push_back(trimmed);
            }
        }
        return entries;
    }

    [[nodiscard]] std::string SerializeRegistry(const std::vector<std::string>& entries) {
        std::string raw;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i > 0) {
                raw.push_back('\n');
            }
            raw.append(entries[i]);
        }
        return raw;
    }

    [[nodiscard]] bool IsContextScope(std::string_view scope) {
        return scope.starts_with(kContextPrefix);
    }

    [[nodiscard]] std::string NormalizeScope(std::string_view scope) {
        std::string normalized = Zeri::Core::Utils::ToLower(Zeri::Core::Utils::Trim(scope));
        if (normalized.empty() || normalized == "global") {
            return std::string(kGlobalScope);
        }
        if (!normalized.starts_with(kContextPrefix)) {
            normalized = std::string(kContextPrefix) + normalized;
        }
        return normalized;
    }

    [[nodiscard]] std::string BuildRegistryId(std::string_view name, std::string_view scope) {
        return std::string(scope) + "|" + std::string(name);
    }

    [[nodiscard]] std::pair<std::string, std::string> ParseRegistryId(std::string_view id) {
        const auto split = id.find('|');
        if (split == std::string_view::npos) {
            return { std::string(id), std::string(kGlobalScope) };
        }
        const std::string scope = NormalizeScope(id.substr(0, split));
        const std::string name = std::string(id.substr(split + 1));
        return { name, scope };
    }

    [[nodiscard]] std::string ScopeDisplay(std::string_view scope) {
        if (!IsContextScope(scope)) {
            return "global";
        }
        return std::string(scope);
    }

    [[nodiscard]] std::expected<std::string, Zeri::Engines::ExecutionError> ExtractDefineBody(
        const Zeri::Engines::Command& cmd
    ) {
        const std::string trimmed = Zeri::Core::Utils::Trim(cmd.rawInput);
        const auto quoteStart = trimmed.find('"');
        if (quoteStart == std::string::npos) {
            const std::string fallback = Zeri::Core::Utils::JoinTail(cmd.args, 1);
            if (Zeri::Core::Utils::Trim(fallback).empty()) {
                return std::unexpected(Zeri::Engines::ExecutionError{
                    "CUSTOM_DEFINE_MISSING_ARGS",
                    "Missing name or body for /define.",
                    cmd.rawInput,
                    { "Usage: /define <name> \"<body>\" [--context <ctx>]" }
                });
            }
            return fallback;
        }

        bool escape = false;
        std::size_t quoteEnd = std::string::npos;
        for (std::size_t i = quoteStart + 1; i < trimmed.size(); ++i) {
            const char c = trimmed[i];
            if (escape) {
                escape = false;
                continue;
            }
            if (c == '\\') {
                escape = true;
                continue;
            }
            if (c == '"') {
                quoteEnd = i;
                break;
            }
        }

        if (quoteEnd == std::string::npos) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "CUSTOM_DEFINE_UNCLOSED_BODY",
                "Unclosed quoted body in /define.",
                cmd.rawInput,
                { "Usage: /define <name> \"<body>\" [--context <ctx>]" }
            });
        }

        return trimmed.substr(quoteStart + 1, quoteEnd - quoteStart - 1);
    }

    [[nodiscard]] bool IsKnownContext(std::string_view context) {
        const std::string normalized = Zeri::Core::Utils::ToLower(Zeri::Core::Utils::Trim(context));
        if (normalized.empty()) {
            return false;
        }

        const auto& catalog = Zeri::Core::HelpCatalog::Instance();
        if (catalog.FindContext(normalized) != nullptr) {
            return true;
        }

        static const std::set<std::string> kFallbackContexts = {
            "global", "code", "customcommand", "ai", "math", "sandbox", "lua", "python", "ruby", "js", "ts"
        };
        return kFallbackContexts.contains(normalized);
    }

    [[nodiscard]] std::expected<std::string, Zeri::Engines::ExecutionError> ValidateCommandName(
        std::string_view name,
        std::string_view rawInput
    ) {
        const std::string trimmed = Zeri::Core::Utils::Trim(name);
        if (trimmed.empty()) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "CUSTOM_DEFINE_MISSING_ARGS",
                "Missing name or body for /define.",
                std::string(rawInput),
                { "Usage: /define <name> \"<body>\" [--context <ctx>]" }
            });
        }
        if (trimmed.find_first_of(" \t\r\n|") != std::string::npos) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "CUSTOM_DEFINE_INVALID_NAME",
                "Custom command name cannot contain whitespace or '|'.",
                std::string(rawInput),
                { "Choose a single token name, for example: build, show7, deploy." }
            });
        }
        return trimmed;
    }

    [[nodiscard]] std::map<std::string, std::any> SerializeDefinition(
        const Zeri::Engines::Defaults::CustomCommandDefinition& definition
    ) {
        std::map<std::string, std::any> out;
        out["name"] = definition.name;
        out["body"] = definition.body;
        out["scope"] = definition.scope;
        return out;
    }

}

namespace Zeri::Engines::Defaults {

    std::string CustomCommandContext::BuildCommandKey(std::string_view registryId) {
        return "custom::commands::entry::" + std::string(registryId);
    }

    std::string CustomCommandContext::BuildLegacyCommandKey(std::string_view name) {
        return "custom::commands::" + std::string(name);
    }

    std::string CustomCommandContext::RegistryKey() {
        return "custom::commands::__registry__";
    }

    std::string CustomCommandContext::SchemaVersionKey() {
        return "custom::commands::__schema_version__";
    }

    void CustomCommandContext::WriteRegistry(Zeri::Core::RuntimeState& state, const std::vector<std::string>& ids) {
        state.SetPersistedVariable(RegistryKey(), SerializeRegistry(ids));
    }

    std::expected<std::vector<CustomCommandDefinition>, ExecutionError> CustomCommandContext::ReadDefinitions(
        Zeri::Core::RuntimeState& state
    ) {
        const auto registryAny = state.GetPersistedVariable(RegistryKey());
        const auto registryRaw = AnyToString(registryAny);
        const std::vector<std::string> registryEntries = registryRaw.has_value() ? ParseRegistry(*registryRaw) : std::vector<std::string>{};

        const auto schemaVersion = AnyToInt64(state.GetPersistedVariable(SchemaVersionKey()));
        if (!schemaVersion.has_value() || *schemaVersion < kCustomCommandSchemaVersion) {
            std::vector<CustomCommandDefinition> migrated;
            std::vector<std::string> migratedIds;
            for (const auto& legacyName : registryEntries) {
                const auto body = AnyToString(state.GetPersistedVariable(BuildLegacyCommandKey(legacyName)));
                if (!body.has_value() || Zeri::Core::Utils::Trim(*body).empty()) {
                    continue;
                }
                CustomCommandDefinition definition;
                definition.name = legacyName;
                definition.body = *body;
                definition.scope = std::string(kGlobalScope);
                const std::string id = BuildRegistryId(definition.name, definition.scope);
                state.SetPersistedVariable(BuildCommandKey(id), SerializeDefinition(definition));
                migrated.push_back(std::move(definition));
                migratedIds.push_back(id);
            }
            WriteRegistry(state, migratedIds);
            state.SetPersistedVariable(SchemaVersionKey(), static_cast<std::int64_t>(kCustomCommandSchemaVersion));
            return migrated;
        }

        bool rewriteStorage = false;
        std::vector<CustomCommandDefinition> definitions;
        definitions.reserve(registryEntries.size());

        for (const auto& registryId : registryEntries) {
            const auto [fallbackName, fallbackScope] = ParseRegistryId(registryId);
            auto value = state.GetPersistedVariable(BuildCommandKey(registryId));
            if (!value.has_value()) {
                continue;
            }

            CustomCommandDefinition definition;
            if (const auto rawRecord = AnyToMap(value); rawRecord.has_value()) {
                const auto nameAny = rawRecord->contains("name") ? rawRecord->at("name") : std::any{};
                const auto bodyAny = rawRecord->contains("body") ? rawRecord->at("body") : std::any{};
                const auto scopeAny = rawRecord->contains("scope") ? rawRecord->at("scope") : std::any{};

                const auto name = AnyToString(nameAny);
                const auto body = AnyToString(bodyAny);
                const auto scope = AnyToString(scopeAny);

                definition.name = Zeri::Core::Utils::Trim(name.value_or(fallbackName));
                definition.body = body.value_or("");
                definition.scope = NormalizeScope(scope.value_or(fallbackScope));
                rewriteStorage = rewriteStorage || !scope.has_value();
            } else if (const auto legacyBody = AnyToString(value); legacyBody.has_value()) {
                definition.name = fallbackName;
                definition.body = *legacyBody;
                definition.scope = std::string(kGlobalScope);
                rewriteStorage = true;
            } else {
                return std::unexpected(ExecutionError{
                    "CUSTOM_STORAGE_INVALID_ENTRY",
                    "Custom command storage contains an unsupported entry format.",
                    BuildCommandKey(registryId),
                    { "Delete the corrupted custom command entry from storage and redefine it." }
                });
            }

            if (definition.name.empty() || Zeri::Core::Utils::Trim(definition.body).empty()) {
                continue;
            }
            definitions.push_back(std::move(definition));
        }

        std::vector<CustomCommandDefinition> deduplicated;
        std::vector<std::string> deduplicatedIds;
        std::set<std::string> seen;
        for (const auto& definition : definitions) {
            const std::string id = BuildRegistryId(definition.name, definition.scope);
            if (seen.contains(id)) {
                rewriteStorage = true;
                continue;
            }
            seen.insert(id);
            deduplicated.push_back(definition);
            deduplicatedIds.push_back(id);
        }

        if (rewriteStorage) {
            for (const auto& definition : deduplicated) {
                const std::string id = BuildRegistryId(definition.name, definition.scope);
                state.SetPersistedVariable(BuildCommandKey(id), SerializeDefinition(definition));
            }
            WriteRegistry(state, deduplicatedIds);
            state.SetPersistedVariable(SchemaVersionKey(), static_cast<std::int64_t>(kCustomCommandSchemaVersion));
        }

        return deduplicated;
    }

    std::expected<std::vector<CustomCommandDefinition>, ExecutionError> CustomCommandContext::FindByName(
        Zeri::Core::RuntimeState& state,
        std::string_view name
    ) {
        const std::string wanted = Zeri::Core::Utils::Trim(name);
        if (wanted.empty()) {
            return std::vector<CustomCommandDefinition>{};
        }

        auto definitionsResult = ReadDefinitions(state);
        if (!definitionsResult.has_value()) {
            return std::unexpected(definitionsResult.error());
        }

        std::vector<CustomCommandDefinition> matches;
        for (const auto& definition : *definitionsResult) {
            if (definition.name == wanted) {
                matches.push_back(definition);
            }
        }
        return matches;
    }

    std::expected<std::optional<CustomCommandDefinition>, ExecutionError> CustomCommandContext::ResolveForInvocation(
        Zeri::Core::RuntimeState& state,
        std::string_view name,
        std::string_view activeContext
    ) {
        const std::string wanted = Zeri::Core::Utils::Trim(name);
        if (wanted.empty() || Zeri::Engines::IsGlobalCommand(wanted)) {
            return std::optional<CustomCommandDefinition>{};
        }

        auto matchesResult = FindByName(state, wanted);
        if (!matchesResult.has_value()) {
            return std::unexpected(matchesResult.error());
        }

        const std::string activeScope = std::string(kContextPrefix) + Zeri::Core::Utils::ToLower(Zeri::Core::Utils::Trim(activeContext));
        for (const auto& definition : *matchesResult) {
            if (definition.scope == activeScope) {
                return definition;
            }
        }
        for (const auto& definition : *matchesResult) {
            if (definition.scope == kGlobalScope) {
                return definition;
            }
        }
        return std::optional<CustomCommandDefinition>{};
    }

    std::expected<std::vector<std::string>, ExecutionError> CustomCommandContext::SplitBody(
        std::string_view body,
        std::string_view sourceInput
    ) {
        std::vector<std::string> commands;
        std::string current;
        bool inQuotes = false;
        bool escape = false;

        auto flushCurrent = [&commands, &current]() {
            std::string trimmed = Zeri::Core::Utils::Trim(current);
            if (!trimmed.empty()) {
                commands.push_back(std::move(trimmed));
            }
            current.clear();
        };

        for (char c : body) {
            if (escape) {
                current.push_back(c);
                escape = false;
                continue;
            }

            if (c == '\\') {
                current.push_back(c);
                escape = true;
                continue;
            }

            if (c == '"') {
                inQuotes = !inQuotes;
                current.push_back(c);
                continue;
            }

            if (c == ';' && !inQuotes) {
                flushCurrent();
                continue;
            }

            current.push_back(c);
        }
        flushCurrent();

        if (inQuotes) {
            return std::unexpected(ExecutionError{
                "CUSTOM_BODY_UNCLOSED_QUOTE",
                "Custom command body has an unclosed quoted string.",
                std::string(sourceInput),
                { "Close every opening quote in the body before saving the command." }
            });
        }

        if (commands.empty()) {
            return std::unexpected(ExecutionError{
                "CUSTOM_BODY_EMPTY",
                "Custom command body does not contain executable sub-commands.",
                std::string(sourceInput),
                { "Define at least one slash command, for example: /define build \"/save ; /run\"." }
            });
        }

        for (std::size_t i = 0; i < commands.size(); ++i) {
            const std::string trimmed = Zeri::Core::Utils::Trim(commands[i]);
            if (!trimmed.starts_with('/')) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_BODY_INVALID_SUBCOMMAND",
                    "Custom command step " + std::to_string(i + 1) + " is not a slash command: " + trimmed,
                    std::string(sourceInput),
                    { "Use explicit slash commands only, for example: /save ; /run." }
                });
            }
        }

        return commands;
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
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_DEFINE_MISSING_ARGS",
                    "Missing name or body for /define.",
                    cmd.rawInput,
                    { "Usage: /define <name> \"<body>\" [--context <ctx>]" }
                });
            }

            auto nameResult = ValidateCommandName(cmd.args[0], cmd.rawInput);
            if (!nameResult.has_value()) {
                return std::unexpected(nameResult.error());
            }

            std::string scope = std::string(kGlobalScope);
            if (const auto flagIt = cmd.flags.find("context"); flagIt != cmd.flags.end()) {
                const std::string contextValue = Zeri::Core::Utils::ToLower(Zeri::Core::Utils::Trim(flagIt->second));
                if (contextValue.empty() || contextValue == "true") {
                    return std::unexpected(ExecutionError{
                        "CUSTOM_DEFINE_INVALID_CONTEXT",
                        "Missing value for --context.",
                        cmd.rawInput,
                        { "Usage: /define <name> \"<body>\" --context <ctx>" }
                    });
                }
                if (!IsKnownContext(contextValue)) {
                    return std::unexpected(ExecutionError{
                        "CUSTOM_DEFINE_UNKNOWN_CONTEXT",
                        "Unknown context for --context: " + contextValue,
                        cmd.rawInput,
                        { "Run /context to list valid context names, then retry /define." }
                    });
                }
                scope = std::string(kContextPrefix) + contextValue;
            }

            auto bodyResult = ExtractDefineBody(cmd);
            if (!bodyResult.has_value()) {
                return std::unexpected(bodyResult.error());
            }

            const std::string body = Zeri::Core::Utils::Trim(*bodyResult);
            auto parsedBody = SplitBody(body, cmd.rawInput);
            if (!parsedBody.has_value()) {
                return std::unexpected(parsedBody.error());
            }

            auto definitionsResult = ReadDefinitions(state);
            if (!definitionsResult.has_value()) {
                return std::unexpected(definitionsResult.error());
            }
            auto definitions = std::move(*definitionsResult);

            bool replaced = false;
            for (auto& definition : definitions) {
                if (definition.name == *nameResult && definition.scope == scope) {
                    definition.body = body;
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                definitions.push_back(CustomCommandDefinition{
                    .name = *nameResult,
                    .body = body,
                    .scope = scope
                });
            }

            std::vector<std::string> ids;
            ids.reserve(definitions.size());
            for (const auto& definition : definitions) {
                const std::string id = BuildRegistryId(definition.name, definition.scope);
                state.SetPersistedVariable(BuildCommandKey(id), SerializeDefinition(definition));
                ids.push_back(id);
            }
            WriteRegistry(state, ids);
            state.SetPersistedVariable(SchemaVersionKey(), static_cast<std::int64_t>(kCustomCommandSchemaVersion));

            return "Custom command defined: " + *nameResult + " [" + ScopeDisplay(scope) + "]";
        }

        if (cmd.commandName == "list") {
            auto definitionsResult = ReadDefinitions(state);
            if (!definitionsResult.has_value()) {
                return std::unexpected(definitionsResult.error());
            }

            auto definitions = std::move(*definitionsResult);
            if (definitions.empty()) {
                return "No custom commands defined.";
            }

            std::ranges::sort(definitions, [](const auto& lhs, const auto& rhs) {
                if (lhs.scope == rhs.scope) {
                    return lhs.name < rhs.name;
                }
                if (lhs.scope == kGlobalScope) {
                    return true;
                }
                if (rhs.scope == kGlobalScope) {
                    return false;
                }
                return lhs.scope < rhs.scope;
            });

            std::string output = "Custom commands:\n";
            output += "  Name | Scope\n";
            output += "  ---- | -----\n";
            for (const auto& definition : definitions) {
                output += "  " + definition.name + " | " + ScopeDisplay(definition.scope) + "\n";
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
            return std::unexpected(ExecutionError{
                "CUSTOM_RUN_ROUTED",
                "Custom command execution is routed by the dispatcher.",
                cmd.rawInput,
                { "Run /run <name> from the prompt; execution is handled before context dispatch." }
            });
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

            const std::string activeContext = Zeri::Core::Utils::ToLower(
                state.GetCurrentContext() != nullptr ? state.GetCurrentContext()->GetName() : "global"
            );
            auto matchesResult = FindByName(state, cmd.args[0]);
            if (!matchesResult.has_value()) {
                return std::unexpected(matchesResult.error());
            }
            std::optional<CustomCommandDefinition> resolved;
            const std::string activeScope = std::string(kContextPrefix) + activeContext;
            for (const auto& definition : *matchesResult) {
                if (definition.scope == activeScope) {
                    resolved = definition;
                    break;
                }
            }
            if (!resolved.has_value()) {
                for (const auto& definition : *matchesResult) {
                    if (definition.scope == kGlobalScope) {
                        resolved = definition;
                        break;
                    }
                }
            }
            if (!resolved.has_value()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_NOT_FOUND",
                    "Custom command not found: " + cmd.args[0],
                    cmd.rawInput
                });
            }
            const auto& definition = *resolved;
            return "Name: " + definition.name + "\nScope: " + ScopeDisplay(definition.scope) + "\nBody: " + definition.body;
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

            const std::string wanted = Zeri::Core::Utils::Trim(cmd.args[0]);
            auto allDefinitionsResult = ReadDefinitions(state);
            if (!allDefinitionsResult.has_value()) {
                return std::unexpected(allDefinitionsResult.error());
            }
            auto allDefinitions = std::move(*allDefinitionsResult);

            std::vector<CustomCommandDefinition> matches;
            for (const auto& definition : allDefinitions) {
                if (definition.name == wanted) {
                    matches.push_back(definition);
                }
            }

            if (matches.empty()) {
                return std::unexpected(ExecutionError{
                    "CUSTOM_NOT_FOUND",
                    "Custom command not found: " + wanted,
                    cmd.rawInput
                });
            }

            CustomCommandDefinition selected = matches.front();
            if (matches.size() > 1U) {
                std::vector<std::string> options;
                options.reserve(matches.size());
                for (const auto& candidate : matches) {
                    options.push_back(candidate.name + " [" + ScopeDisplay(candidate.scope) + "]");
                }
                const auto selection = terminal.SelectMenu(
                    "Multiple custom commands named '" + wanted + "'. Select one to delete:",
                    options
                );
                if (!selection.has_value() || *selection < 0 || *selection >= static_cast<int>(matches.size())) {
                    return Info("Delete canceled.");
                }
                selected = matches[static_cast<std::size_t>(*selection)];
            }

            std::vector<CustomCommandDefinition> remaining;
            remaining.reserve(allDefinitions.size());
            for (const auto& definition : allDefinitions) {
                if (definition.name == selected.name && definition.scope == selected.scope) {
                    continue;
                }
                remaining.push_back(definition);
            }

            std::vector<std::string> ids;
            ids.reserve(remaining.size());
            for (const auto& definition : remaining) {
                const std::string id = BuildRegistryId(definition.name, definition.scope);
                state.SetPersistedVariable(BuildCommandKey(id), SerializeDefinition(definition));
                ids.push_back(id);
            }
            WriteRegistry(state, ids);
            state.SetPersistedVariable(SchemaVersionKey(), static_cast<std::int64_t>(kCustomCommandSchemaVersion));

            return "Custom command deleted: " + selected.name + " [" + ScopeDisplay(selected.scope) + "]";
        }

        if (cmd.commandName == "help") {
            return "CustomCommand commands:\n"
                   "  /define <name> \"<body>\" [--context <ctx>]\n"
                   "  /list\n"
                   "  /run <name>\n"
                   "  /show <name>\n"
                   "  /delete <name>\n"
                   "\n"
                   "Body rules: use slash sub-commands only, split with ';', fail-fast on first error.";
        }

        return std::unexpected(ExecutionError{
            "CUSTOM_UNKNOWN_OPERATION",
            "Unknown custom command operation: " + cmd.commandName,
            cmd.rawInput,
            { "Use /help to list supported commands." }
        });
    }

}

/*
CustomCommandContext.cpp
Implements custom command management and storage migration:
  - Registry schema v2 stores each definition with explicit {name, body, scope}.
  - Legacy entries without scope are auto-migrated to scope=global.
  - Bodies are parsed as executable slash-command chains separated by ';',
    quote-aware and escape-aware, with empty steps ignored.
  - /list, /show, /delete are scope-aware, including interactive disambiguation
    when deleting homonymous commands across scopes.
*/
