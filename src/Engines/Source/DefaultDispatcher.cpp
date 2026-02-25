#include "../Include/DefaultDispatcher.h"

namespace Zeri::Engines::Defaults {

    ExecutionType DefaultDispatcher::Classify(const Command& cmd) {
        if (cmd.type == InputType::ContextSwitch) return ExecutionType::ContextCommand;
        if (cmd.type == InputType::SystemOp) return ExecutionType::SystemProcess;
        if (cmd.commandName == "@context_eval") return ExecutionType::Expression;
        if (!cmd.commandName.empty() && cmd.commandName.ends_with(".lua")) return ExecutionType::LuaScript;
        if (cmd.type == InputType::Command) return ExecutionType::Builtin;
        return ExecutionType::Unknown;
    }
}