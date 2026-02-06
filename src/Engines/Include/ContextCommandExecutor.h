#pragma once
#include "Interface/IExecutor.h"
#include "../../Core/Include/ContextManager.h"

namespace Zeri::Engines::Defaults {

    class ContextCommandExecutor : public IExecutor {
    public:
        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state
        ) override;

        [[nodiscard]] ExecutionType GetType() const override;

    private:
        Zeri::Core::ContextManager m_contextManager;
    };

}

/*
Header for `ContextCommandExecutor`.
Handles / prefixed commands that activate contexts
*/