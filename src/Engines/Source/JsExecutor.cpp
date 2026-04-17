#include "../Include/JsExecutor.h"

#include <chrono>
#include <filesystem>
#include <fstream>
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

        const std::vector<std::string> args = { "run", scriptPath.string() };

        auto result = m_bridge.Run(
            m_binary,
            args,
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
