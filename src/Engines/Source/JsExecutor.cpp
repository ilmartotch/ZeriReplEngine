#include "../Include/JsExecutor.h"

#include <sstream>
#include <vector>

namespace {

    [[nodiscard]] std::string JoinArgs(const std::vector<std::string>& args) {
        std::ostringstream stream;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                stream << ' ';
            }
            stream << args[i];
        }
        return stream.str();
    }

}

namespace Zeri::Engines::Defaults {

    JsExecutor::JsExecutor(const Zeri::Core::ScriptRuntime& runtime, bool typescript)
        : m_binary(runtime.binary)
        , m_typescript(typescript) {
    }

    ExecutionOutcome JsExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)state;

        if (m_binary.empty()) {
            return std::unexpected(ExecutionError{
                "JS_RUNTIME_MISSING",
                "Bun runtime not available in Zeri environment.",
                cmd.rawInput,
                { "Install bun and ensure it is available in PATH." }
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
                "JS_INPUT_ERR",
                "No JS/TS code provided.",
                cmd.rawInput,
                { "Provide code through rawInput, args, or pipeInput." }
            });
        }

        const std::vector<std::string> args = { "eval", script };

        return m_bridge.Run(
            m_binary,
            args,
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
JsExecutor.cpp
Bun-only JS/TS execution through ProcessBridge.
Both JavaScript and TypeScript are executed via `bun eval <script>`.
stdout/stderr are streamed to the terminal callbacks.
*/
