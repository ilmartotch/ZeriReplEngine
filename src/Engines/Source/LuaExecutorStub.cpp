#include "../Include/LuaExecutorStub.h"
#include "../include/Interface/IDispatcher.h"

namespace Zeri::Engines::Defaults {

    ExecutionOutcome LuaExecutorStub::Execute(
        const Command& cmd, 
        Zeri::Core::RuntimeState& state
    ) {
        // Future: Initialize lua state, bind RuntimeState, load file.
        return "Lua Script Execution Stub for: " + cmd.commandName;
    }

    ExecutionType LuaExecutorStub::GetType() const {
        return ExecutionType::LuaScript;
    }

}

/*
Implementation of `LuaExecutorStub`.
Currently returns a placeholder string.
In v0.2+, this will interface with the Lua C API or C++ wrappers to execute external scripts.
*/
