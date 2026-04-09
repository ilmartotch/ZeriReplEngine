#include "../Include/PythonExecutor.h"

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

    PythonExecutor::PythonExecutor(const Zeri::Core::ScriptRuntime& runtime)
        : m_binary(runtime.binary) {
    }

    ExecutionOutcome PythonExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)state;

        if (m_binary.empty()) {
            return std::unexpected(ExecutionError{
                "PYTHON_RUNTIME_MISSING",
                "Python runtime not available in Zeri environment (python3 not found).",
                cmd.rawInput,
                { "Install python3 and ensure it is available in PATH." }
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
                "PYTHON_INPUT_ERR",
                "No Python code provided.",
                cmd.rawInput,
                { "Provide Python code through rawInput, args, or pipeInput." }
            });
        }

        return m_bridge.Run(
            m_binary,
            { "-u", "-c", script },
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
PythonExecutor.cpp
Implementation of Python executor with one-shot external runtime invocation:
`<binary> -u -c <script>`. The `-u` flag forces unbuffered output.
stdout is streamed through Write() and stderr through WriteError().
When runtime is missing, it returns typed error `PYTHON_RUNTIME_MISSING`.
*/
