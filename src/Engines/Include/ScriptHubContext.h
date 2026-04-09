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
Contesto hub `zeri::code>` che non esegue codice direttamente: effettua solo
il dispatch verso i context linguaggio (`lua`, `python`, `js`, `ts`, `ruby`)
e fornisce help con stato runtime derivato da SystemGuard.
*/
