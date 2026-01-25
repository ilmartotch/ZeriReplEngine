#include "../Include/StandardDispatcher.h"

namespace Zeri::Engines::Defaults {

    ExecutionType StandardDispatcher::Classify(const Command& cmd) {
        if (cmd.commandName == "exit" || cmd.commandName == "help" || cmd.commandName == "set" || cmd.commandName == "get") {
            return ExecutionType::Builtin;
        }
        if (cmd.commandName.ends_with(".lua")) {
            return ExecutionType::LuaScript;
        }
        if (cmd.commandName.starts_with("!")) {
            return ExecutionType::SystemProcess;
        }
        return ExecutionType::Unknown;
    }

}

/*
Implementation of `StandardDispatcher`.
Routing Rules:
- Reserved keywords (exit, help, set, get) -> Builtin.
- Ends with .lua -> LuaScript.
- Starts with ! -> SystemProcess.
- Everything else -> Unknown.
*/
