#pragma once

#include "BaseContext.h"

#include <string>
#include <vector>

namespace Zeri::Engines::Defaults {

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

    private:
        [[nodiscard]] static std::string BuildCommandKey(const std::string& name);
        [[nodiscard]] static std::string RegistryKey();
        [[nodiscard]] static std::vector<std::string> ReadRegistry(Zeri::Core::RuntimeState& state);
        static void WriteRegistry(Zeri::Core::RuntimeState& state, const std::vector<std::string>& names);
    };

}

/*
CustomCommandContext.h
Definisce il contesto isolato `zeri::custom>` per la gestione dei comandi utente
personalizzati. I comandi restano confinati a questo context (vincolo v1) con
persistenza su RuntimeState PersistedScope sotto namespace `custom::commands::*`.
*/
