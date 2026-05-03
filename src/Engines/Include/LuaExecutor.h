#pragma once

#include "Interface/IExecutor.h"
#include "../../Core/Include/SystemGuard.h"
#include "ProcessBridge.h"
#include "../../../engine/src/bridge/ZeriWireSidecarBridge.h"

#include <string>

namespace Zeri::Engines::Defaults {

    class LuaExecutor final : public IExecutor {
    public:
        explicit LuaExecutor(const Zeri::Core::ScriptRuntime& runtime);
        ~LuaExecutor() override;

        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

        [[nodiscard]] ExecutionType GetType() const override {
            return ExecutionType::LuaScript;
        }

    private:
        Zeri::Engines::Defaults::ProcessBridge m_bridge;
        Zeri::Bridge::ZeriWireSidecarBridge m_sidecarBridge;
        std::string m_binary;
    };

}

/*
LuaExecutor.h — Executor for Lua scripts via external process bridge.

Responsabilità:
  - Receives a ScriptRuntime with the resolved luajit binary path.
  - Execute(): Runs Lua code via ProcessBridge with `-e` flag.
  - Streams stdout/stderr to ITerminal in real time.

Dipendenze: IExecutor, ProcessBridge, SystemGuard (ScriptRuntime).
*/