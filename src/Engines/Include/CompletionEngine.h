#pragma once
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

        static constexpr std::array<CommandSpec, 11> kCommands = {{
            {"/help",   "Mostra help",                ReplContext::Global},
            {"/exit",   "Chiude la REPL",             ReplContext::Global},
            {"/back",   "Torna indietro",             ReplContext::Global},
            {"/set",    "<key> <value>",              ReplContext::Global},
            {"/get",    "<key>",                      ReplContext::Global},
            {"/calc",   "<a> <op> <b>",               ReplContext::Math},
            {"/logic",  "<op> <v1> <v2>",             ReplContext::Math},
            {"/list",   "Mostra moduli",              ReplContext::Sandbox},
            {"/build",  "<module>",                   ReplContext::Sandbox},
            {"/run",    "<module>",                   ReplContext::Sandbox},
            {"/start",  "Avvia wizard",               ReplContext::Setup}
        }};

        static constexpr std::array<std::string_view, 4> kContexts = {
            "$global", "$math", "$sandbox", "$setup"
        };
    };

}
