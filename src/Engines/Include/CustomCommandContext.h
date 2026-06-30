#pragma once

#include "BaseContext.h"

#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace Zeri::Engines::Defaults {

    struct CustomCommandDefinition {
        std::string name;
        std::string body;
        std::string scope;
    };

    class CustomCommandContext : public BaseContext {
    public:
        [[nodiscard]] std::string GetName() const override { return "customcommand"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::custom>"; }

        void OnEnter(Zeri::Ui::ITerminal& terminal) override;

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

        [[nodiscard]] static std::expected<std::optional<CustomCommandDefinition>, ExecutionError> ResolveForInvocation(
            Zeri::Core::RuntimeState& state,
            std::string_view name,
            std::string_view activeContext
        );

        [[nodiscard]] static std::expected<std::vector<CustomCommandDefinition>, ExecutionError> FindByName(
            Zeri::Core::RuntimeState& state,
            std::string_view name
        );

        [[nodiscard]] static std::expected<std::vector<std::string>, ExecutionError> SplitBody(
            std::string_view body,
            std::string_view sourceInput
        );

    private:
        [[nodiscard]] static std::string BuildCommandKey(std::string_view registryId);
        [[nodiscard]] static std::string BuildLegacyCommandKey(std::string_view name);
        [[nodiscard]] static std::string RegistryKey();
        [[nodiscard]] static std::string SchemaVersionKey();
        [[nodiscard]] static std::expected<std::vector<CustomCommandDefinition>, ExecutionError> ReadDefinitions(
            Zeri::Core::RuntimeState& state
        );
        static void WriteRegistry(Zeri::Core::RuntimeState& state, const std::vector<std::string>& ids);
    };

}

/*
CustomCommandContext.h
Defines the isolated `zeri::custom>` context for user-defined commands.
Definitions are managed from this context and persisted under
`custom::commands::*`, while invocation is resolved by the engine
dispatcher according to scope/precedence rules.
*/
