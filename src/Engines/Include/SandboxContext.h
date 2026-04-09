#pragma once

#include "BaseContext.h"
#include "ProcessBridge.h"
#include "../../Modules/Include/ModuleManager.h"

#include <filesystem>
#include <optional>
#include <vector>

namespace Zeri::Engines::Defaults {

    class SandboxContext : public BaseContext {
    public:
        void OnEnter(Zeri::Ui::ITerminal& terminal) override;

        [[nodiscard]] std::string GetName() const override { return "sandbox"; }
        [[nodiscard]] std::string GetPrompt() const override { return "zeri::sandbox>"; }

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

    private:
        ExecutionOutcome HandleOpen(const Command& cmd, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);
        ExecutionOutcome HandleSetIde(const Command& cmd, Zeri::Core::RuntimeState& state);
        [[nodiscard]] static std::string ResolveSandboxIde(const Zeri::Core::RuntimeState& state);
        ExecutionOutcome RunExternalTarget(const Command& cmd, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);
        ExecutionOutcome RunBlockingExternal(
            const std::filesystem::path& executable,
            const std::vector<std::string>& args,
            const std::optional<std::filesystem::path>& cwd,
            Zeri::Ui::ITerminal& terminal
        );

        ExecutionOutcome ListModules(Zeri::Core::RuntimeState& state);
        ExecutionOutcome BuildModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);
        ExecutionOutcome RunModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);

        ProcessBridge m_bridge;
    };

}

/*
SandboxContext.h — Module development and code execution environment.

Responsabilità:
  - Gestione moduli (list, build, run) tramite ModuleManager + ProcessBridge.
  - Integrazione IDE (open, set-ide).
  - Placeholder monitoring (watch).

Dipendenze: BaseContext, ProcessBridge, ModuleManager.

QA Changes:
  - Ripulito commento: rimosso riferimento a ScriptRunner (eliminato dal progetto).
  - Prompt statico `zeri::sandbox>`.
*/
