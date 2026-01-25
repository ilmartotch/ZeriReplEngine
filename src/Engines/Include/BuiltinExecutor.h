#pragma once
#include "Interface/IExecutor.h"
#include "Interface/IDispatcher.h"

namespace Zeri::Engines::Defaults {

    class BuiltinExecutor : public IExecutor {
    public:
        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd, 
            Zeri::Core::RuntimeState& state
        ) override;

        [[nodiscard]] ExecutionType GetType() const override;
    };

}

/*
Header for `BuiltinExecutor`.
Handles internal commands that modify the REPL state or provide help/info.
*/
