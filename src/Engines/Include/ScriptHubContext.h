#pragma once

#include "BaseContext.h"

namespace Zeri::Engines::Defaults {

    class ScriptHubContext : public BaseContext {
    public:
        [[nodiscard]] std::string GetName() const override { return "code"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::code>"; }

        void OnEnter(Zeri::Ui::ITerminal& terminal) override;

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;
    };

}

/*
ScriptHubContext.h
Hub context `zeri::code>` that does not execute code directly: it only
dispatches to language contexts (`lua`, `python`, `js`, `ts`, `ruby`)
and provides help with runtime status derived from SystemGuard.
*/
