#include "../Include/PythonExecutor.h"
#include "../Include/ExecutorUtils.h"
#include "../../Core/Include/StringUtils.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

namespace {
    inline constexpr const char* kPythonPathEnvVar = "ZERI_PYTHON_PATH";
    inline constexpr const char* kPythonEngineDir = "runtime";
    inline constexpr const char* kPythonBootstrapScript = "bootstrap_python.py";

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

    [[nodiscard]] Zeri::Engines::ExecutionOutcome ExecuteOneShot(
        Zeri::Engines::Defaults::ProcessBridge& bridge,
        const std::string& executable,
        const std::string& script,
        Zeri::Ui::ITerminal& terminal
    ) {
        return bridge.Run(
            executable,
            { "-u", "-c", script },
            [&terminal](const std::string& line) {
                terminal.Write(line);
            },
            [&terminal](const std::string& line) {
                terminal.WriteError(line);
            }
        );
    }

    [[nodiscard]] Zeri::Engines::ExecutionOutcome ExecuteViaSidecar(
        Zeri::Bridge::ZeriWireSidecarBridge& sidecar,
        const std::string& script,
        Zeri::Ui::ITerminal& terminal,
        const std::string& context
    ) {
        auto result = sidecar.Execute(script, terminal);
        if (!result.has_value()) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "PYTHON_SIDECAR_EXEC_ERR",
                "Python sidecar execution failed.",
                context,
                { result.error().Format() }
            });
        }

        const auto& payload = result.value();

        if (!payload.stdoutText.empty()) {
            terminal.WriteLine(payload.stdoutText);
        }

        if (!payload.stderrText.empty()) {
            terminal.WriteError(payload.stderrText);
        }

        if (payload.exitCode != 0) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "PYTHON_EXEC_ERR",
                "Python runtime execution failed with non-zero exit code.",
                context,
                { "Exit code: " + std::to_string(payload.exitCode) }
            });
        }

        return Zeri::Engines::ExecutionMessage{};
    }

}

namespace Zeri::Engines::Defaults {

    PythonExecutor::PythonExecutor(const Zeri::Core::ScriptRuntime& runtime)
        : m_binary(runtime.binary) {
    }

    PythonExecutor::~PythonExecutor() {
        m_sidecarBridge.Shutdown();
    }

    ExecutionOutcome PythonExecutor::Execute(
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
                "PYTHON_INPUT_ERR",
                "No Python code provided.",
                cmd.rawInput,
                { "Provide Python code through rawInput, args, or pipeInput." }
            });
        }

        const std::string executable = Zeri::Engines::Utils::ResolveExecutable({
            ResolveExecutableOverride(kPythonPathEnvVar),
            m_binary,
            "python"
        });
        const std::filesystem::path bootstrapPath = Zeri::Engines::Utils::ResolveBootstrapPath(kPythonEngineDir, kPythonBootstrapScript);

        if (m_sidecarBridge.IsAlive()) {
            return ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
        }

        const bool launched = m_sidecarBridge.Launch(
            executable,
            { "-u" },
            bootstrapPath.string()
        );

        if (launched) {
            return ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
        }

        terminal.WriteError("[WARN] Python sidecar launch failed, falling back to one-shot execution.");
        return ExecuteOneShot(m_bridge, executable, script, terminal);
    }

}

/*
PythonExecutor.cpp
Implementation of Python executor with one-shot external runtime invocation:
`<binary> -u -c <script>`. The `-u` flag forces unbuffered output.
stdout is streamed through Write() and stderr through WriteError().
When runtime is missing, it returns typed error `PYTHON_RUNTIME_MISSING`.
*/
