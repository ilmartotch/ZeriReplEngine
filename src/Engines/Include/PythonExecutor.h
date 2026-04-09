#pragma once

#include "Interface/IExecutor.h"
#include "../../Core/Include/SystemGuard.h"
#include "ProcessBridge.h"

#include <string>

namespace Zeri::Engines::Defaults {

    class PythonExecutor final : public IExecutor {
    public:
        explicit PythonExecutor(const Zeri::Core::ScriptRuntime& runtime);

        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

        [[nodiscard]] ExecutionType GetType() const override {
            return ExecutionType::PythonScript;
        }

    private:
        Zeri::Engines::Defaults::ProcessBridge m_bridge;
        std::string m_binary;
    };

}

/*
PythonExecutor.h
Python runtime-based executor using ProcessBridge for one-shot execution
with binary resolved by SystemGuard (python3), without dependencies on
SidecarProcessBridge or ZeriWire protocol.
*/
