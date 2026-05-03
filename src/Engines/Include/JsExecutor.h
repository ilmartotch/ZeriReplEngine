#pragma once

#include "Interface/IExecutor.h"
#include "../../Core/Include/SystemGuard.h"
#include "ProcessBridge.h"
#include "../../../engine/src/bridge/ZeriWireSidecarBridge.h"

#include <string>

namespace Zeri::Engines::Defaults {

    class JsExecutor final : public IExecutor {
    public:
        explicit JsExecutor(const Zeri::Core::ScriptRuntime& runtime, bool typescript = false);
        ~JsExecutor() override;

        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

        [[nodiscard]] ExecutionType GetType() const override {
            return ExecutionType::JsScript;
        }

    private:
        Zeri::Engines::Defaults::ProcessBridge m_bridge;
        Zeri::Bridge::ZeriWireSidecarBridge m_sidecarBridge;
        std::string m_binary;
        bool m_typescript{ false };
    };

}

/*
JsExecutor.h
JavaScript/TypeScript executor using ProcessBridge in one-shot mode.
The runtime is resolved by SystemGuard as Bun-only for both JS and TS.
The `m_typescript` flag keeps context identity and messaging aligned.
*/
