#pragma once
#include "Interface/IExecutor.h"
#include "../../Core/Include/ContextManager.h"

namespace Zeri::Engines::Defaults {

    class ContextCommandExecutor : public IExecutor {
    public:
        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

        [[nodiscard]] ExecutionType GetType() const override;

    private:
        Zeri::Core::ContextManager m_contextManager;
    };

}

/*
Handles / prefixed commands that activate contexts
*/