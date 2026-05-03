#include "../Include/JsExecutor.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>

namespace {
    inline constexpr const char* kBunPathEnvVar = "ZERI_BUN_PATH";
    inline constexpr const char* kJsBootstrapPath = "runtime/bootstrap_bun.js";

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

        return "bun";
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
                "JS_SIDECAR_EXEC_ERR",
                "JS/TS sidecar execution failed.",
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
                "JS_EXEC_ERR",
                "Bun runtime execution failed with non-zero exit code.",
                context,
                { "Exit code: " + std::to_string(payload.exitCode) }
            });
        }

        return Zeri::Engines::ExecutionMessage{};
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

    [[nodiscard]] std::filesystem::path BuildTempScriptPath(bool typescript) {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto ext = typescript ? ".ts" : ".js";
        const std::string fileName = "zeri_bun_exec_" + std::to_string(now) + ext;
        return std::filesystem::temp_directory_path() / fileName;
    }

}

namespace Zeri::Engines::Defaults {

    JsExecutor::JsExecutor(const Zeri::Core::ScriptRuntime& runtime, bool typescript)
        : m_binary(runtime.binary)
        , m_typescript(typescript) {
    }

    JsExecutor::~JsExecutor() {
        m_sidecarBridge.Shutdown();
    }

    ExecutionOutcome JsExecutor::Execute(
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
                "JS_INPUT_ERR",
                "No JS/TS code provided.",
                cmd.rawInput,
                { "Provide code through rawInput, args, or pipeInput." }
            });
        }

        const auto scriptPath = BuildTempScriptPath(m_typescript);
        {
            std::ofstream file(scriptPath, std::ios::binary | std::ios::trunc);
            if (!file.is_open()) {
                return std::unexpected(ExecutionError{
                    "JS_FILE_WRITE_ERR",
                    "Failed to create temporary JS/TS script file.",
                    cmd.rawInput,
                    { "Check temporary directory permissions and retry." }
                });
            }
            file << script;
            if (!file.good()) {
                return std::unexpected(ExecutionError{
                    "JS_FILE_WRITE_ERR",
                    "Failed to write temporary JS/TS script content.",
                    cmd.rawInput,
                    { "Check disk availability and temporary directory permissions." }
                });
            }
        }

        const std::string executable = ResolveExecutable(m_binary, kBunPathEnvVar);
        const std::filesystem::path bootstrapPath = ResolveBootstrapPath(kJsBootstrapPath);

        ExecutionOutcome result;

        if (m_sidecarBridge.IsAlive()) {
            result = ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
        } else {
            const bool launched = m_sidecarBridge.Launch(executable, {}, bootstrapPath.string());
            if (launched) {
                result = ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
            } else {
                terminal.WriteError("[WARN] JS/TS sidecar launch failed, falling back to one-shot execution.");
                result = m_bridge.Run(
                    executable,
                    { "run", scriptPath.string() },
                    [&terminal](const std::string& line) {
                        terminal.Write(line);
                    },
                    [&terminal](const std::string& line) {
                        terminal.WriteError(line);
                    }
                );

                if (result.has_value()) {
                    const int exitCode = m_bridge.WaitForExit();
                    if (exitCode != 0) {
                        result = std::unexpected(ExecutionError{
                            "JS_EXEC_ERR",
                            "Bun runtime execution failed with non-zero exit code.",
                            cmd.rawInput,
                            { "Check the script output for runtime errors.", "Exit code: " + std::to_string(exitCode) }
                        });
                    }
                }
            }
        }

        std::error_code removeError;
        std::filesystem::remove(scriptPath, removeError);

        return result;
    }

}

/*
JsExecutor.cpp
Bun-only JS/TS execution through ProcessBridge.
Both JavaScript and TypeScript are executed via temporary files and `bun run <tempfile>`.
stdout/stderr are streamed to the terminal callbacks.
*/
