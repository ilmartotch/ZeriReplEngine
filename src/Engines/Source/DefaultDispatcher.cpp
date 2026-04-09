#include "../Include/DefaultDispatcher.h"

namespace Zeri::Engines::Defaults {

    ExecutionType DefaultDispatcher::Classify(const Command& cmd) {
        if (cmd.type == InputType::ContextSwitch) return ExecutionType::ContextCommand;
        if (cmd.type == InputType::SystemOp) return ExecutionType::SystemProcess;
        if (cmd.type == InputType::Expression) return ExecutionType::Expression;
        if (cmd.commandName == "@context_eval") return ExecutionType::Expression;
        if (!cmd.commandName.empty() && cmd.commandName.ends_with(".lua")) return ExecutionType::LuaScript;
        if (cmd.type == InputType::Command) return ExecutionType::Builtin;
        return ExecutionType::Unknown;
    }
}

/*
DefaultDispatcher.cpp — Implementation of concrete IDispatcher.

Classify():
  Maps InputType + command properties to ExecutionType. Recognises
  ContextSwitch, SystemOp, Expression, @context_eval, .lua suffix,
  and generic Command inputs.

Dipendenze: IDispatcher (Command, ExecutionType).
*/