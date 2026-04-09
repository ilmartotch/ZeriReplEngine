#pragma once
#include <array>
#include <vector>
#include <string>
#include <string_view>
#include <optional>

namespace Zeri::Engines {

    enum class ReplContext {
        Global,
        Math,
        Sandbox,
        Setup
    };

    struct Completion {
        std::string text;
        std::string description;
    };

    class CompletionEngine {
    public:
        CompletionEngine();

        [[nodiscard]] std::vector<Completion> GetCompletions(std::string_view input, ReplContext context) const;
        [[nodiscard]] std::optional<std::string_view> GetHint(std::string_view input, ReplContext context) const;

    private:
        struct CommandSpec {
            std::string_view command;
            std::string_view usageHint;
            ReplContext context;
        };

        static constexpr std::array<CommandSpec, 19> kCommands = {{
            {"/help", "Show help for current context", ReplContext::Global},
            {"/context", "List available contexts", ReplContext::Global},
            {"/exit", "Exit the REPL", ReplContext::Global},
            {"/back", "Return to previous context", ReplContext::Global},
            {"/save", "Save session state to disk", ReplContext::Global},
            {"/set", "<key> <value>", ReplContext::Global},
            {"/get", "<key>", ReplContext::Global},
            {"/lua", "<script>", ReplContext::Global},
            {"/eval", "<expr>", ReplContext::Math},
            {"/fn", "f(x) = expression", ReplContext::Math},
            {"/vars", "List variables", ReplContext::Math},
            {"/fns", "List functions", ReplContext::Math},
            {"/promote", "<var> <scope>", ReplContext::Math},
            {"/calc", "<a> <op> <b>", ReplContext::Math},
            {"/logic", "<op> <v1> <v2>", ReplContext::Math},
            {"/list", "List modules", ReplContext::Sandbox},
            {"/build", "<module>", ReplContext::Sandbox},
            {"/run", "<module>", ReplContext::Sandbox},
            {"/start", "Start configuration wizard", ReplContext::Setup}
        }};

        static constexpr std::array<std::string_view, 4> kContexts = {
            "$global", "$math", "$sandbox", "$setup"
        };
    };

}

/*
CompletionEngine.h — Standalone completion data for IDE integration.

Defines the static kCommands array mapping each command to its usage hint
and parent context (Global, Math, Sandbox, Setup). Used for programmatic
autocompletion and hint generation.

QA Changes:
  - Array expanded 16 -> 19: added /save (Global), /lua (Global), and /context (Global).
  - All descriptions translated from Italian to English.
*/
