#include "../Include/RubyExecutor.h"

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

    RubyExecutor::RubyExecutor(const Zeri::Core::ScriptRuntime& runtime)
        : m_binary(runtime.binary) {
    }

    ExecutionOutcome RubyExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)state;

        if (m_binary.empty()) {
            return std::unexpected(ExecutionError{
                "RUBY_RUNTIME_MISSING",
                "Ruby runtime not available in Zeri environment.",
                cmd.rawInput,
                { "Install Ruby 3.3+ (YJIT) and ensure it is available in PATH." }
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
                "RUBY_INPUT_ERR",
                "No Ruby code provided.",
                cmd.rawInput,
                { "Provide Ruby code through rawInput, args, or pipeInput." }
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
RubyExecutor.cpp
Implementation of Ruby executor with one-shot external runtime invocation:
`ruby -e <script>`. Ruby 3.3+ enables YJIT by default, so no extra flags
are required. stdout is streamed with Write(), stderr with WriteError().
*/
