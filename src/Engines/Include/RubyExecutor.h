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
Executor Ruby runtime-based che usa ProcessBridge per esecuzione one-shot inline
via `ruby -e`, usando il runtime risolto da SystemGuard (`GetRuntime("ruby")`).
La gestione output/error segue i callback separati stdout/stderr verso ITerminal.
*/
