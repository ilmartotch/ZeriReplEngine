#pragma once

#include "Interface/IExecutor.h"
#include "../../Core/Include/SystemGuard.h"
#include "ProcessBridge.h"
#include "../../../engine/src/bridge/ZeriWireSidecarBridge.h"

#include <string>

namespace Zeri::Engines::Defaults {

    class RubyExecutor final : public IExecutor {
    public:
        explicit RubyExecutor(const Zeri::Core::ScriptRuntime& runtime);
        ~RubyExecutor() override;

        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;
        [[nodiscard]] bool CancelActiveExecution();

        [[nodiscard]] ExecutionType GetType() const override {
            return ExecutionType::RubyScript;
        }

    private:
        Zeri::Engines::Defaults::ProcessBridge m_bridge;
        Zeri::Bridge::ZeriWireSidecarBridge m_sidecarBridge;
        std::string m_binary;
    };

}

/*
RubyExecutor.h
Runtime-based Ruby executor that uses ProcessBridge for one-shot inline
execution via `ruby -e`, using the runtime resolved by SystemGuard (`GetRuntime("ruby")`).
Output/error handling follows separate stdout/stderr callbacks to ITerminal.
*/
