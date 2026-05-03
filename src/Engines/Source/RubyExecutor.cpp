#include "../Include/RubyExecutor.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

namespace {
    inline constexpr const char* kRubyPathEnvVar = "ZERI_RUBY_PATH";
    inline constexpr const char* kRubyBootstrapPath = "runtime/bootstrap_ruby.rb";

    [[nodiscard]] std::string ResolveExecutable(const std::string& runtimeBinary, const char* envVarName) {
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

        if (!runtimeBinary.empty()) {
            return runtimeBinary;
        }

        return "ruby";
    }

    [[nodiscard]] std::filesystem::path ResolveBootstrapPath(const char* relativeBootstrapPath) {
        std::error_code ec;

        const std::filesystem::path directPath(relativeBootstrapPath);
        if (std::filesystem::exists(directPath, ec)) {
            return directPath;
        }

        const std::filesystem::path nestedPath = std::filesystem::path("..") / relativeBootstrapPath;
        ec.clear();
        if (std::filesystem::exists(nestedPath, ec)) {
            return nestedPath;
        }

        return directPath;
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
            terminal.WriteError(payload.stderrText);
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
                terminal.WriteError(line);
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

    RubyExecutor::~RubyExecutor() {
        m_sidecarBridge.Shutdown();
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

        const std::string executable = ResolveExecutable(m_binary, kRubyPathEnvVar);
        const std::filesystem::path bootstrapPath = ResolveBootstrapPath(kRubyBootstrapPath);
        const bool bootstrapExists = std::filesystem::exists(bootstrapPath);

        if (!bootstrapExists) {
            terminal.WriteError("[WARN] Ruby sidecar bootstrap not found, falling back to one-shot execution.");
            return ExecuteOneShot(m_bridge, executable, script, terminal, cmd.rawInput);
        }

        if (m_sidecarBridge.IsAlive()) {
            return ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
        }

        const bool launched = m_sidecarBridge.Launch(executable, {}, bootstrapPath.string());
        if (launched) {
            return ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
        }

        terminal.WriteError("[WARN] Ruby sidecar launch failed, falling back to one-shot execution.");
        return ExecuteOneShot(m_bridge, executable, script, terminal, cmd.rawInput);
    }

}

/*
RubyExecutor.cpp
Implementation of Ruby executor with one-shot external runtime invocation:
`ruby -e <script>`. Ruby 3.3+ enables YJIT by default, so no extra flags
are required. stdout is streamed with Write(), stderr with WriteError().
*/
