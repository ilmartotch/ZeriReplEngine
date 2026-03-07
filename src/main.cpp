#include "Core/Include/RuntimeState.h"
#include "Core/Include/SystemGuard.h"
#include "Engines/Include/GlobalContext.h"
#include "Engines/Include/MathContext.h"
#include "Engines/Include/SandboxContext.h"
#include "Engines/Include/SetupContext.h"
#include "Ui/Include/TerminalUi.h"
#include "Engines/Include/MetaParser.h"
#include "Engines/Include/DefaultDispatcher.h"
#include "Engines/Include/CachedDispatcher.h"

#include <memory>
#include <print>
#include <cctype>
#include <string_view>
#include <vector>
#include <optional>
#include <algorithm>

namespace {
    [[nodiscard]] std::string_view Trim(std::string_view value) {
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return value;
    }

    [[nodiscard]] std::vector<std::string> SplitPipeline(std::string_view input) {
        std::vector<std::string> stages;
        std::string current;
        bool inQuotes = false;
        bool escape = false;

        for (char c : input) {
            if (escape) {
                current.push_back(c);
                escape = false;
                continue;
            }

            if (c == '\\') {
                escape = true;
                current.push_back(c);
                continue;
            }

            if (c == '"') {
                inQuotes = !inQuotes;
                current.push_back(c);
                continue;
            }

            if (c == '|' && !inQuotes) {
                stages.emplace_back(std::string{ Trim(current) });
                current.clear();
                continue;
            }

            current.push_back(c);
        }

        stages.emplace_back(std::string{ Trim(current) });
        return stages;
    }

    [[nodiscard]] std::unique_ptr<Zeri::Engines::IContext> BuildContext(const std::string& name) {
        if (name == "math") return std::make_unique<Zeri::Engines::Defaults::MathContext>();
        if (name == "sandbox") return std::make_unique<Zeri::Engines::Defaults::SandboxContext>();
        if (name == "setup") return std::make_unique<Zeri::Engines::Defaults::SetupContext>();
        return nullptr;
    }

    void HandleOutcome(const Zeri::Engines::ExecutionOutcome& outcome, Zeri::Ui::ITerminal& terminal) {
        if (outcome.has_value()) {
            if (!outcome->empty()) terminal.WriteLine(outcome.value());
        } else {
            terminal.WriteError(outcome.error().Format());
        }
    }

    [[nodiscard]] bool SwitchContext(
        const std::string& ctxName,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (ctxName == "global") {
            auto* current = runtimeState.GetCurrentContext();
            while (current && current->GetName() != "global") {
                current->OnExit(terminal);
                runtimeState.PopContext();
                current = runtimeState.GetCurrentContext();
            }

            auto* activeContext = runtimeState.GetCurrentContext();
            if (activeContext != nullptr) {
                activeContext->OnEnter(terminal);
                return true;
            }

            terminal.WriteError("Internal error: no active context available.");
            return false;
        }

        auto nextContext = BuildContext(ctxName);
        if (!nextContext) {
            terminal.WriteError("Unknown context: " + ctxName);
            return false;
        }

        runtimeState.PushContext(std::move(nextContext));
        runtimeState.GetCurrentContext()->OnEnter(terminal);
        return true;
    }

    [[nodiscard]] bool ExecuteStage(
        const std::string& stageInput,
        Zeri::Engines::Defaults::CachedDispatcher& dispatcher,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal,
        std::optional<std::string>& pipedValue
    ) {
        auto dispatchResult = dispatcher.Dispatch(stageInput);
        if (!dispatchResult.has_value()) {
            const auto& err = dispatchResult.error();
            std::string caret(err.position, ' ');
            caret += '^';
            terminal.WriteError(err.message + "\n  " + stageInput + "\n  " + caret);
            return false;
        }

        auto cmd = dispatchResult->command;
        if (cmd.empty()) return true;

        cmd.pipeInput = pipedValue;

        switch (cmd.type) {
        case Zeri::Engines::InputType::ContextSwitch:
            return SwitchContext(cmd.commandName, runtimeState, terminal);

        case Zeri::Engines::InputType::Expression:
        case Zeri::Engines::InputType::Command: {
            auto* currentCtx = runtimeState.GetCurrentContext();
            if (!currentCtx) return false;

            auto outcome = currentCtx->HandleCommand(cmd, runtimeState, terminal);
            HandleOutcome(outcome, terminal);

            if (!outcome.has_value()) return false;
            pipedValue = outcome.value();
            return true;
        }

        default:
            return false;
        }
    }
}

int main() {
    // Core Initialization
    Zeri::Core::RuntimeState runtimeState;
    Zeri::Ui::TerminalUi terminal;
    Zeri::Engines::Defaults::MetaParser parser;
    Zeri::Engines::Defaults::DefaultDispatcher baseDispatcher;
    Zeri::Engines::Defaults::CachedDispatcher dispatcher(parser, baseDispatcher);

    // System Health Check
    auto health = Zeri::Core::SystemGuard::CheckEnvironment();
    if (!health.IsReady()) {
        Zeri::Core::SystemGuard::PrintGuide(health, terminal);
    }

    // Bootstrapping (Root Context)
    runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::GlobalContext>());
    runtimeState.GetCurrentContext()->OnEnter(terminal);

    // Main Loop
    while (!runtimeState.IsExitRequested()) {
        auto* currentCtx = runtimeState.GetCurrentContext();
        std::string prompt = currentCtx ? (currentCtx->GetPrompt() + "> ") : "zeri> ";

        auto inputOpt = terminal.ReadLine(prompt);
        if (!inputOpt.has_value()) break;

        std::string input = *inputOpt;
        if (Trim(input).empty()) continue;

        auto stages = SplitPipeline(input);
        if (std::ranges::any_of(stages, [](const std::string& s) { return Trim(s).empty(); })) {
            terminal.WriteError("Invalid pipeline syntax: empty stage detected around '|'.")
;
            continue;
        }

        std::optional<std::string> pipedValue;
        bool ok = true;
        for (const auto& stage : stages) {
            if (!ExecuteStage(stage, dispatcher, runtimeState, terminal, pipedValue)) {
                ok = false;
                break;
            }
        }

        if (!ok) {
            continue;
        }
    }

    terminal.WriteLine("Goodbye.");
    return 0;
}

/*
Integrated enhanced pipeline processing with context-aware command execution.
Improved error reporting and handling for pipeline stages.
Refactored context switching logic for clarity and reliability.
*/