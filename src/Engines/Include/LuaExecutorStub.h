#pragma once
#include "Interface/IExecutor.h"

namespace Zeri::Engines::Defaults {

    class LuaExecutorStub : public IExecutor {
    public:
        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd, 
            Zeri::Core::RuntimeState& state
        ) override;

        [[nodiscard]] ExecutionType GetType() const override;
    };

}

/*
Header for `LuaExecutorStub`.
Placeholder for future Lua integration using a library like sol2.
*/
