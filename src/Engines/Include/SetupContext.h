#pragma once

#include "BaseContext.h"

namespace Zeri::Engines::Defaults {

    class SetupContext : public BaseContext {
    public:
        void OnEnter(Zeri::Ui::ITerminal& terminal) override;
        [[nodiscard]] std::string GetName() const override { return "setup"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::setup"; }

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const std::string& commandName,
            const std::vector<std::string>& args,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

    private:
        void RunWizard(Zeri::Ui::ITerminal& terminal, Zeri::Core::RuntimeState& state);
    };

}

/*
FILE DOCUMENTATION:
SetupContext Header.
This context is triggered for initial configuration.
It guides the user through setting up preferences like the preferred IDE.
*/
