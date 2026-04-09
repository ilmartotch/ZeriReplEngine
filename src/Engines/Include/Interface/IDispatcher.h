#pragma once
#include "../Command.h"

namespace Zeri::Engines {

    enum class ExecutionType {
        Builtin,
        LuaScript,
        PythonScript,
        JsScript,
        RubyScript,
        CppRepl,
        RustRepl,
        ShellScript,
        SystemProcess,
        Expression,
        ContextCommand,
        Unknown
    };

    class IDispatcher {
    public:
        virtual ~IDispatcher() = default;

        [[nodiscard]] virtual ExecutionType Classify(const Command& cmd) = 0;
    };

}

/*
Interface `IDispatcher`.
Contract for the routing logic. The Dispatcher's sole responsibility is to inspect a `Command`
and determine *who* should execute it (Builtin, Lua, System, etc.). It does not execute the command itself.
*/
