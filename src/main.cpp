#include "Core/Include/RuntimeState.h"
#include "Core/Include/SystemGuard.h"
#include "Engines/Include/GlobalContext.h"
#include "Engines/Include/MathContext.h"
#include "Engines/Include/SandboxContext.h"
#include "Engines/Include/SetupContext.h" // Added Setup
#include "Ui/Include/TerminalUi.h"
#include "Engines/Include/MetaParser.h"

#include <memory>
#include <print>

namespace {
    void HandleOutcome(const Zeri::Engines::ExecutionOutcome& outcome, Zeri::Ui::TerminalUi& terminal) {
        if (outcome.has_value()) {
            if (!outcome->empty()) terminal.WriteLine(outcome.value());
        } else {
            terminal.WriteError("[" + outcome.error().code + "] " + outcome.error().message);
        }
    }
}

int main() {
    // 1. Core Initialization
    Zeri::Core::RuntimeState runtimeState;
    Zeri::Ui::TerminalUi terminal;
    Zeri::Engines::Defaults::MetaParser parser;

    // 2. System Health Check
    auto health = Zeri::Core::SystemGuard::CheckEnvironment();
    if (!health.IsReady()) {
        Zeri::Core::SystemGuard::PrintGuide(health);
    }

    // 3. Bootstrapping (Root Context)
    runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::GlobalContext>());
    runtimeState.GetCurrentContext()->OnEnter(terminal);

    // 4. Main Loop
    while (!runtimeState.IsExitRequested()) {
        auto* currentCtx = runtimeState.GetCurrentContext();
        std::string prompt = currentCtx ? (currentCtx->GetPrompt() + "> ") : "zeri> ";
        
        auto inputOpt = terminal.ReadLine(prompt);
        if (!inputOpt.has_value()) break;

        std::string input = *inputOpt;
        if (input.empty()) continue;

        auto parseResult = parser.Parse(input);
        if (!parseResult.has_value()) {
            terminal.WriteError(parseResult.error().message);
            continue;
        }

        auto& cmd = parseResult.value();
        if (cmd.empty()) continue;

        switch (cmd.type) {
            case Zeri::Engines::InputType::ContextSwitch: {
                std::string ctxName = cmd.target;
                if (ctxName == "math") {
                    runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::MathContext>());
                } else if (ctxName == "sandbox") {
                    runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::SandboxContext>());
                } else if (ctxName == "setup") {
                    runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::SetupContext>());
                } else if (ctxName == "global") {
                    terminal.WriteLine("Global context is the root.");
                    continue;
                } else {
                    terminal.WriteError("Unknown context: " + ctxName);
                    continue;
                }
                runtimeState.GetCurrentContext()->OnEnter(terminal);
                break;
            }

            case Zeri::Engines::InputType::Command: {
                if (cmd.target == "back") {
                    runtimeState.PopContext();
                    runtimeState.GetCurrentContext()->OnEnter(terminal);
                    continue;
                }
                auto outcome = currentCtx->HandleCommand(cmd.target, cmd.args, runtimeState, terminal);
                HandleOutcome(outcome, terminal);
                break;
            }

            default:
                terminal.WriteError("Syntax not supported.");
                break;
        }
    }

    terminal.WriteLine("Goodbye.");
    return 0;
}

/*
FILE DOCUMENTATION:
Main Loop v0.3.6.
Integrated SystemGuard for startup health checks.
Added $setup context switch to trigger the configuration wizard.
*/