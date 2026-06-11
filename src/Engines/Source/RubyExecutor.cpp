#include "../Include/RubyExecutor.h"
#include "../Include/ExecutorUtils.h"
#include "../../Core/Include/StringUtils.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

namespace {
    inline constexpr const char* kRubyPathEnvVar = "ZERI_RUBY_PATH";
    inline constexpr const char* kRubyEngineDir = "runtime";
    inline constexpr const char* kRubyBootstrapScript = "bootstrap_ruby.rb";

    [[nodiscard]] std::string ResolveExecutableOverride(const char* envVarName) {
#ifdef _WIN32
        char* envValueRaw = nullptr;
        size_t envValueLength = 0;
        const errno_t envError = _dupenv_s(&envValueRaw, &envValueLength, envVarName);
        if (envError == 0 && envValueRaw != nullptr && envValueLength > 1) {
            std::string resolved(envValueRaw);
            std::free(envValueRaw);
            return resolved;
        }
        if (envValueRaw != nullptr) {
            std::free(envValueRaw);
        }
#else
        const char* envValue = std::getenv(envVarName);
        if (envValue != nullptr && *envValue != '\0') {
            return std::string(envValue);
        }
#endif
        return {};
    }

    [[nodiscard]] Zeri::Engines::ExecutionOutcome ExecuteViaSidecar(
        Zeri::Bridge::ZeriWireSidecarBridge& sidecar,
        const std::string& script,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal,
        const std::string& context
    ) {
        auto result = sidecar.Execute(script, state, terminal);
        if (!result.has_value()) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "RUBY_SIDECAR_EXEC_ERR",
                "Ruby sidecar execution failed.",
                context,
                { result.error().Format() }
            });
        }

        const auto& payload = result.value();

        if (!payload.stdoutText.empty()) {
            terminal.WriteLine(payload.stdoutText);
        }

        if (!payload.stderrText.empty()) {
            terminal.WriteError("[ZERI][RUNTIME-030] Ruby runtime stderr: " + payload.stderrText + " Hint: inspect Ruby output and runtime dependencies.");
        }

        if (payload.exitCode != 0) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "RUBY_EXEC_ERR",
                "Ruby runtime execution failed with non-zero exit code.",
                context,
                { "Exit code: " + std::to_string(payload.exitCode) }
            });
        }

        return Zeri::Engines::ExecutionMessage{};
    }

    [[nodiscard]] Zeri::Engines::ExecutionOutcome ExecuteOneShot(
        Zeri::Engines::Defaults::ProcessBridge& bridge,
        const std::string& executable,
        const std::string& script,
        Zeri::Ui::ITerminal& terminal,
        const std::string& context
    ) {
        auto result = bridge.Run(
            executable,
            { "-e", script },
            [&terminal](const std::string& line) {
                terminal.Write(line);
            },
            [&terminal](const std::string& line) {
                terminal.WriteError("[ZERI][RUNTIME-031] Ruby one-shot stderr: " + line + " Hint: inspect Ruby output and runtime dependencies.");
            }
        );

        if (!result.has_value()) {
            return result;
        }

        const int exitCode = bridge.WaitForExit();
        if (exitCode != 0) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "RUBY_EXEC_ERR",
                "Ruby runtime execution failed with non-zero exit code.",
                context,
                { "Exit code: " + std::to_string(exitCode) }
            });
        }

        return result;
    }

}

namespace Zeri::Engines::Defaults {

    RubyExecutor::RubyExecutor(const Zeri::Core::ScriptRuntime& runtime)
        : m_binary(runtime.binary) {
    }

    RubyExecutor::~RubyExecutor() {
        m_sidecarBridge.Shutdown();
    }

    bool RubyExecutor::CancelActiveExecution() {
        m_sidecarBridge.Shutdown();
        m_bridge.Terminate();
        return true;
    }

    ExecutionOutcome RubyExecutor::Execute(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        (void)state;

        std::string script = cmd.rawInput;
        if (script.empty()) {
            if (!cmd.args.empty()) {
                script = Zeri::Core::Utils::JoinArgs(cmd.args);
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

        const std::string executable = Zeri::Engines::Utils::ResolveExecutable({
            ResolveExecutableOverride(kRubyPathEnvVar),
            m_binary,
            "ruby"
        });
        const std::filesystem::path bootstrapPath = Zeri::Engines::Utils::ResolveBootstrapPath(kRubyEngineDir, kRubyBootstrapScript);
        const bool bootstrapExists = std::filesystem::exists(bootstrapPath);

        if (!bootstrapExists) {
            terminal.WriteError("[ZERI][RUNTIME-032] Ruby sidecar bootstrap not found, falling back to one-shot execution. Hint: ensure runtime/bootstrap_ruby.rb is packaged.");
            return ExecuteOneShot(m_bridge, executable, script, terminal, cmd.rawInput);
        }

        if (m_sidecarBridge.IsAlive()) {
            return ExecuteViaSidecar(m_sidecarBridge, script, state, terminal, cmd.rawInput);
        }

        const bool launched = m_sidecarBridge.Launch(executable, {}, bootstrapPath.string());
        if (launched) {
            return ExecuteViaSidecar(m_sidecarBridge, script, state, terminal, cmd.rawInput);
        }

        terminal.WriteError("[ZERI][RUNTIME-033] Ruby sidecar launch failed, falling back to one-shot execution. Hint: verify Ruby installation and executable path.");
        return ExecuteOneShot(m_bridge, executable, script, terminal, cmd.rawInput);
    }

}

/*
RubyExecutor.cpp
Implementation of Ruby executor with one-shot external runtime invocation:
`ruby -e <script>`. Ruby 3.3+ enables YJIT by default, so no extra flags
are required. stdout is streamed with Write(), stderr with WriteError().
*/
