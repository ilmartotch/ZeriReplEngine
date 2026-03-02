#pragma once

#include "BaseContext.h"
#include "ProcessBridge.h"
#include "../../Modules/Include/ModuleManager.h"

namespace Zeri::Engines::Defaults {

    class SandboxContext : public BaseContext {
    public:
        void OnEnter(Zeri::Ui::ITerminal& terminal) override;
        [[nodiscard]] std::string GetName() const override { return "sandbox"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::sandbox"; }

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

    private:
        ExecutionOutcome ListModules(Zeri::Core::RuntimeState& state);
        ExecutionOutcome BuildModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);
        ExecutionOutcome RunModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);

        ProcessBridge m_bridge;
    };

}

/*
This context allows users to manage their custom modules.
It provides commands to list, build (via CMake), and run modules using the ProcessBridge.
It acts as the development hub for the REPL.
*/
