#pragma once
#include <memory>
#include "IDispatcher.h"
#include "../Command.h"
#include "../ExecutionResult.h"
#include "../../../Core/Include/RuntimeState.h"

namespace Zeri::Engines {

    class IExecutor {
    public:
        virtual ~IExecutor() = default;

        [[nodiscard]] virtual ExecutionOutcome Execute(
            const Command& cmd, 
            Zeri::Core::RuntimeState& state
        ) = 0;

        [[nodiscard]] virtual ExecutionType GetType() const = 0;
    };

    using ExecutorPtr = std::unique_ptr<IExecutor>;

}

/*
Contract for any "Black Box" capable of executing code.
The `Execute` method receives the `RuntimeState` reference, allowing the executor to modify the application's
memory or context side-effects. `GetType` is used by the ExtensionManager to register the executor against
a specific `ExecutionType`.
*/
