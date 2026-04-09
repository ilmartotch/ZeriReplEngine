#include "../Include/LuaExecutor.h"

#include <sstream>
#include <vector>

namespace {
    [[nodiscard]] std::string JoinArgs(const std::vector<std::string>& args) {
        std::ostringstream oss;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) oss << ' ';
            oss << args[i];
        }
        return oss.str();
    }
}

namespace Zeri::Engines::Defaults {

    LuaExecutor::LuaExecutor(const Zeri::Core::ScriptRuntime& runtime)
        : m_binary(runtime.binary) {
    }

    ExecutionOutcome LuaExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)state;

        if (m_binary.empty()) {
            return std::unexpected(ExecutionError{
                "LUA_RUNTIME_MISSING",
                "Lua runtime not available in Zeri environment (luajit not found).",
                cmd.rawInput,
                { "Install luajit and ensure it is available in PATH." }
            });
        }

        std::string script = cmd.rawInput;
        if (script.empty()) {
            if (!cmd.args.empty()) {
                script = JoinArgs(cmd.args);
            } else if (cmd.pipeInput.has_value()) {
                script = *cmd.pipeInput;
            }
        }

        if (script.empty()) {
            return std::unexpected(ExecutionError{
                "LUA_INPUT_ERR",
                "No Lua code provided.",
                cmd.rawInput,
                { "Provide Lua code through rawInput, args, or pipeInput." }
            });
        }

        return m_bridge.Run(
            m_binary,
            { "-e", script },
            [&terminal](const std::string& line) {
                terminal.Write(line);
            },
            [&terminal](const std::string& line) {
                terminal.WriteError(line);
            }
        );
    }

}

/*
LuaExecutor.cpp — Implementation of LuaExecutor.

Execute():
  Validates the luajit runtime binary, extracts Lua source from
  rawInput/args/pipeInput, then runs `luajit -e <script>` via ProcessBridge.
  Streams stdout via terminal.Write() and stderr via terminal.WriteError().

Dependencies: ProcessBridge, ITerminal.
*/