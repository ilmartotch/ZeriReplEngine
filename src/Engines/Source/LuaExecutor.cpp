#include "../Include/LuaExecutor.h"

#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <vector>

namespace {
    inline constexpr const char* kLuaJitPathEnvVar = "ZERI_LUAJIT_PATH";
    inline constexpr const char* kLuaBootstrapPath = "runtime/bootstrap_lua.lua";

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

        return "luajit";
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
                "LUA_SIDECAR_EXEC_ERR",
                "Lua sidecar execution failed.",
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
                "LUA_EXEC_ERR",
                "Lua runtime execution failed with non-zero exit code.",
                context,
                { "Exit code: " + std::to_string(payload.exitCode) }
            });
        }

        return Zeri::Engines::ExecutionMessage{};
    }

    [[nodiscard]] std::string WrapLuaScript(const std::string& source) {
        std::ostringstream wrapped;
        wrapped << "local __zeri_fn = function()\n";
        wrapped << source << "\n";
        wrapped << "end\n";
        wrapped << "local __zeri_ok, __zeri_result = pcall(__zeri_fn)\n";
        wrapped << "if not __zeri_ok then error(__zeri_result) end\n";
        wrapped << "if __zeri_result ~= nil then print(__zeri_result) end\n";
        return wrapped.str();
    }

    [[nodiscard]] Zeri::Engines::ExecutionOutcome ExecuteOneShot(
        Zeri::Engines::Defaults::ProcessBridge& bridge,
        const std::string& executable,
        const std::string& script,
        Zeri::Ui::ITerminal& terminal,
        const std::string& context
    ) {
        const std::string wrappedScript = WrapLuaScript(script);

        auto result = bridge.Run(
            executable,
            { "-e", wrappedScript },
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
                "LUA_EXEC_ERR",
                "Lua runtime execution failed with non-zero exit code.",
                context,
                { "Exit code: " + std::to_string(exitCode) }
            });
        }

        return result;
    }

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

    LuaExecutor::~LuaExecutor() {
        m_sidecarBridge.Shutdown();
    }

    ExecutionOutcome LuaExecutor::Execute(
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
                "LUA_INPUT_ERR",
                "No Lua code provided.",
                cmd.rawInput,
                { "Provide Lua code through rawInput, args, or pipeInput." }
            });
        }

        const std::string executable = ResolveExecutable(m_binary, kLuaJitPathEnvVar);
        const std::filesystem::path bootstrapPath = ResolveBootstrapPath(kLuaBootstrapPath);
        const bool bootstrapExists = std::filesystem::exists(bootstrapPath);

        if (!bootstrapExists) {
            terminal.WriteError("[WARN] Lua sidecar bootstrap not found, falling back to one-shot execution.");
            return ExecuteOneShot(m_bridge, executable, script, terminal, cmd.rawInput);
        }

        if (m_sidecarBridge.IsAlive()) {
            return ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
        }

        const bool launched = m_sidecarBridge.Launch(executable, {}, bootstrapPath.string());
        if (launched) {
            return ExecuteViaSidecar(m_sidecarBridge, script, terminal, cmd.rawInput);
        }

        terminal.WriteError("[WARN] Lua sidecar launch failed, falling back to one-shot execution.");
        return ExecuteOneShot(m_bridge, executable, script, terminal, cmd.rawInput);
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