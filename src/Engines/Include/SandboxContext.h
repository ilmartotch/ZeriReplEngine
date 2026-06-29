#pragma once

#include "BaseContext.h"
#include "ProcessBridge.h"
#include "../../Modules/Include/ModuleManager.h"

#include <filesystem>
#include <optional>
#include <string_view>
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
        [[nodiscard]] static std::string ResolveSandboxIde(const Zeri::Core::RuntimeState& state);
        ExecutionOutcome RunExternalFilePath(
            std::string_view filePathInput,
            const Command& origin,
            Zeri::Ui::ITerminal& terminal
        );
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

Responsibilities:
  - Module management (list, build, run) through ModuleManager + ProcessBridge.
  - IDE integration (open).
  - Placeholder monitoring (watch).

Dependencies: BaseContext, ProcessBridge, ModuleManager.
*/
