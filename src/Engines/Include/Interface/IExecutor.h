#pragma once
#include <memory>
#include "IDispatcher.h"
#include "../Command.h"
#include "../ExecutionResult.h"
#include "../../../Core/Include/RuntimeState.h"
#include "../../../Ui/Include/ITerminal.h"

namespace Zeri::Engines {

    class IExecutor {
    public:
        virtual ~IExecutor() = default;

        [[nodiscard]] virtual ExecutionOutcome Execute(
            const Command& cmd, 
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) = 0;

        [[nodiscard]] virtual ExecutionType GetType() const = 0;
    };

    using ExecutorPtr = std::unique_ptr<IExecutor>;

}

/*
Contract for any "Black Box" capable of executing code.
The `Execute` method receives the `RuntimeState` reference, allowing the executor to modify the application's
memory or context side-effects, and the active `ITerminal` reference to emit output during execution without
constructor-level terminal coupling. `GetType` is used by the ExtensionManager to register the executor against
a specific `ExecutionType`.
*/
