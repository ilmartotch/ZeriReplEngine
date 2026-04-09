#include "../Include/SandboxContext.h"
#include "../../Core/Include/RuntimeState.h"
#include "../../Core/Include/SystemGuard.h"
#include <any>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <format>
#include <string_view>
#include <thread>

#ifdef _WIN32
    #include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace {

    [[nodiscard]] std::string ToLower(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return result;
    }

    [[nodiscard]] std::string_view TrimRight(std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
        return value;
    }

    [[nodiscard]] bool IsLikelyInputPrompt(std::string_view chunk) {
        const std::string_view trimmed = TrimRight(chunk);
        if (trimmed.empty()) {
            return false;
        }

        const char tail = trimmed.back();
        if (tail == ':' || tail == '?' || tail == '>') {
            return true;
        }

        const std::string lowered = ToLower(trimmed);
        return lowered.contains("input") || lowered.contains("enter") || lowered.contains("choice");
    }

    [[nodiscard]] bool IsPathLike(std::string_view value) {
        return value.contains('/') || value.contains('\\') || value.contains('.') || value.contains(':');
    }

}

namespace Zeri::Engines::Defaults {

    void SandboxContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("Sandbox environment active \u2014 type /help to list sandbox commands.");
    }

    std::string SandboxContext::ResolveSandboxIde(const Zeri::Core::RuntimeState& state) {
        const std::any raw = state.GetPersistedVariable("sandbox::ide");
        if (raw.type() == typeid(std::string)) {
            const auto ide = std::any_cast<std::string>(raw);
            if (!ide.empty()) {
                return ide;
            }
        }
        return "code";
    }

    ExecutionOutcome SandboxContext::HandleSetIde(const Command& cmd, Zeri::Core::RuntimeState& state) {
        if (cmd.args.empty() || cmd.args[0].empty()) {
            return std::unexpected(ExecutionError{
                "SANDBOX_MISSING_ARGS",
                "Missing IDE name for set-ide.",
                cmd.rawInput,
                { "Usage: /set-ide <name>" }
            });
        }

        state.SetPersistedVariable("sandbox::ide", cmd.args[0]);
        return Zeri::Engines::Success("Sandbox IDE set to: " + cmd.args[0]);
    }

    ExecutionOutcome SandboxContext::HandleOpen(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string ide = ResolveSandboxIde(state);
        const std::string target = cmd.args.empty() ? "." : cmd.args[0];

#ifdef _WIN32
        const int exitCode = m_bridge.ExecuteSync("cmd", { "/c", "start", "", ide, target });
#else
        const int exitCode = m_bridge.ExecuteSync(ide, { target });
#endif
        if (exitCode != 0) {
            return std::unexpected(ExecutionError{
                "SANDBOX_OPEN_FAIL",
                "Failed to open target in IDE.",
                cmd.rawInput,
                { "Verify `sandbox::ide` value and that the IDE binary is in PATH." }
            });
        }

        terminal.WriteInfo("Opened in IDE: " + ide + " -> " + target);
        return Zeri::Engines::Success("Open command dispatched.");
    }

    ExecutionOutcome SandboxContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (cmd.commandName == "open") return HandleOpen(cmd, state, terminal);

        if (cmd.commandName == "set-ide") return HandleSetIde(cmd, state);

        if (cmd.commandName == "watch") {
            return Zeri::Engines::Info(m_bridge.IsRunning()
                ? "Sandbox process monitor: running."
                : "Sandbox process monitor: idle.");
        }

        if (cmd.commandName == "list") return ListModules(state);

        if (cmd.commandName == "build" && !cmd.args.empty()) {
            return BuildModule(cmd.args[0], state, terminal);
        }

        if (cmd.commandName == "build") {
            return std::unexpected(ExecutionError{
                "SANDBOX_MISSING_ARGS",
                "Missing module name for build.",
                cmd.rawInput,
                { "Usage: /build <moduleName>" }
            });
        }

        if (cmd.commandName == "run" && !cmd.args.empty()) {
            return RunExternalTarget(cmd, state, terminal);
        }

        if (cmd.commandName == "run") {
            return std::unexpected(ExecutionError{
                "SANDBOX_MISSING_ARGS",
                "Missing run target.",
                cmd.rawInput,
                { "Usage: /run <moduleName|filePath> [args...] [--cwd <path>]" }
            });
        }

        if (cmd.commandName == "help") {
            const std::string ide = ResolveSandboxIde(state);

            return std::format(
                "Sandbox Context \u2014 Available Commands\n"
                "\n"
                "Global Commands:\n"
                "  /help \u2014 Show help for the active context\n"
                "  /context \u2014 List available contexts\n"
                "  /back \u2014 Return to previous context\n"
                "  /save \u2014 Save session state to disk\n"
                "  /clear \u2014 Clear the chat history\n"
                "  /status \u2014 Show engine diagnostics\n"
                "  /reset \u2014 Reset the current session\n"
                "  /exit \u2014 Exit the REPL\n"
                "\n"
                "Module Commands:\n"
                "  /list \u2014 List all available modules\n"
                "  /build <moduleName> \u2014 Build a module using CMake\n"
                "  /run <moduleName|filePath> [args...] [--cwd <path>] \u2014 Run module or external target\n"
                "\n"
                "IDE + Monitoring:\n"
                "  /open [file] \u2014 Open file/path in configured IDE\n"
                "  /set-ide <name> \u2014 Set preferred IDE command\n"
                "  /watch \u2014 Show current sandbox process status\n"
                "\n"
                "Configured IDE: {}\n",
                ide
            );
        }

        return std::unexpected(ExecutionError{
            "SANDBOX_UNKNOWN",
            "Unknown command in sandbox: " + cmd.commandName,
            cmd.rawInput,
            { "Type /help to list available sandbox commands." }
        });
    }

    ExecutionOutcome SandboxContext::ListModules(Zeri::Core::RuntimeState& state) {
        auto modules = state.GetModuleManager().GetModules();
        if (modules.empty()) return Zeri::Engines::Info("No modules found in 'modules/' directory.");

        std::string result = "Available Modules:\n";
        for (const auto& m : modules) {
            result += std::format("- {} (v{}) [{}]\n", m.name, m.version, m.type);
        }
        return result;
    }

    ExecutionOutcome SandboxContext::BuildModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal) {
        auto modules = state.GetModuleManager().GetModules();
        auto it = std::ranges::find_if(modules, [&](const auto& m) { return m.name == moduleName; });
        if (it == modules.end()) {
            return std::unexpected(ExecutionError{ "BUILD_NOT_FOUND", "Module not found: " + moduleName });
        }

        const fs::path modulePath(it->path);
        terminal.WriteLine("Building module: " + moduleName + " in " + modulePath.string());

        const int eCode = m_bridge.ExecuteSync("cmake", { "--build", "." }, modulePath);
        if (eCode != 0) {
            return std::unexpected(ExecutionError{ "BUILD_FAIL", "Failed to build module (exit code: " + std::to_string(eCode) + ")." });
        }
        return Zeri::Engines::Success("Build succeeded.");
    }

    ExecutionOutcome SandboxContext::RunModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal) {
        auto modules = state.GetModuleManager().GetModules();
        auto it = std::ranges::find_if(modules, [&](const auto& m) { return m.name == moduleName; });
        if (it == modules.end()) {
            return std::unexpected(ExecutionError{ "RUN_NOT_FOUND", "Module not found: " + moduleName });
        }

        if (it->entryPoint.empty()) {
            return std::unexpected(ExecutionError{ "RUN_NO_ENTRY", "Module has no entry point: " + moduleName });
        }

        const fs::path modulePath(it->path);
        const fs::path entryPath = modulePath / it->entryPoint;
        terminal.WriteLine("Running module: " + moduleName);

        return RunBlockingExternal(entryPath, {}, modulePath, terminal);
    }

    ExecutionOutcome SandboxContext::RunExternalTarget(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (cmd.args.empty()) {
            return std::unexpected(ExecutionError{
                "SANDBOX_MISSING_ARGS",
                "Missing target for run.",
                cmd.rawInput,
                { "Usage: /run <moduleName|filePath> [args...] [--cwd <path>]" }
            });
        }

        if (cmd.args.size() == 1 && cmd.flags.empty()) {
            auto modules = state.GetModuleManager().GetModules();
            const auto moduleIt = std::ranges::find_if(modules, [&](const auto& m) { return m.name == cmd.args[0]; });
            if (moduleIt != modules.end()) {
                return RunModule(cmd.args[0], state, terminal);
            }
        }

        std::optional<fs::path> workingDirectory;
        if (const auto cwdIt = cmd.flags.find("cwd"); cwdIt != cmd.flags.end()) {
            if (cwdIt->second.empty() || cwdIt->second == "true") {
                return std::unexpected(ExecutionError{
                    "SANDBOX_MISSING_ARGS",
                    "Missing path for --cwd.",
                    cmd.rawInput,
                    { "Usage: /run <target> [args...] --cwd <path>" }
                });
            }
            workingDirectory = fs::path(cwdIt->second);
        }

        const std::string targetArg = cmd.args[0];
        fs::path targetPath(targetArg);
        bool targetExists = fs::exists(targetPath);

        if (!targetExists && workingDirectory.has_value() && targetPath.is_relative()) {
            targetPath = *workingDirectory / targetPath;
            targetExists = fs::exists(targetPath);
        }

        if (targetExists) {
            targetPath = fs::absolute(targetPath);
            if (fs::is_directory(targetPath)) {
                return std::unexpected(ExecutionError{
                    "SANDBOX_INVALID_TARGET",
                    "Run target is a directory: " + targetPath.string(),
                    cmd.rawInput,
                    { "Provide a file or executable path." }
                });
            }
        }

        fs::path executable;
        std::vector<std::string> processArgs;

        if (targetExists && fs::is_regular_file(targetPath)) {
            const std::string extension = ToLower(targetPath.extension().string());

            auto health = Zeri::Core::SystemGuard::CheckEnvironment();
            auto resolveRuntime = [&](const std::string& language) -> const Zeri::Core::ScriptRuntime* {
                const auto* runtime = health.GetRuntime(language);
                if (runtime == nullptr || !runtime->available) {
                    return nullptr;
                }
                return runtime;
            };

            if (extension == ".py" || extension == ".lua" || extension == ".js" || extension == ".rb" || extension == ".ts") {
                std::string runtimeKey;
                if (extension == ".py") runtimeKey = "python";
                if (extension == ".lua") runtimeKey = "lua";
                if (extension == ".js") runtimeKey = "js";
                if (extension == ".rb") runtimeKey = "ruby";
                if (extension == ".ts") runtimeKey = "js";

                const auto* runtime = resolveRuntime(runtimeKey);
                if (runtime == nullptr) {
                    return std::unexpected(ExecutionError{
                        "SANDBOX_RUNTIME_MISSING",
                        "No runtime available for target extension: " + extension,
                        cmd.rawInput,
                        { "Use /status in global context to inspect runtime availability." }
                    });
                }

                executable = runtime->binary;
                processArgs.push_back(targetPath.string());
                for (size_t i = 1; i < cmd.args.size(); ++i) {
                    processArgs.push_back(cmd.args[i]);
                }

                if (!workingDirectory.has_value()) {
                    workingDirectory = targetPath.parent_path();
                }
            } else {
                executable = targetPath;
                for (size_t i = 1; i < cmd.args.size(); ++i) {
                    processArgs.push_back(cmd.args[i]);
                }

                if (!workingDirectory.has_value()) {
                    workingDirectory = targetPath.parent_path();
                }
            }
        } else {
            if (IsPathLike(targetArg)) {
                return std::unexpected(ExecutionError{
                    "SANDBOX_TARGET_NOT_FOUND",
                    "Run target not found: " + targetArg,
                    cmd.rawInput,
                    { "Use an existing file path or executable name available in PATH." }
                });
            }

            executable = targetArg;
            for (size_t i = 1; i < cmd.args.size(); ++i) {
                processArgs.push_back(cmd.args[i]);
            }
        }

        return RunBlockingExternal(executable, processArgs, workingDirectory, terminal);
    }

    ExecutionOutcome SandboxContext::RunBlockingExternal(
        const std::filesystem::path& executable,
        const std::vector<std::string>& args,
        const std::optional<std::filesystem::path>& cwd,
        Zeri::Ui::ITerminal& terminal
    ) {
        std::atomic<bool> awaitingInput{ false };

        auto outcome = m_bridge.Run(
            executable,
            args,
            [&](const std::string& chunk) {
                terminal.Write(chunk);
                if (IsLikelyInputPrompt(chunk)) {
                    awaitingInput = true;
                }
            },
            [&](const std::string& chunk) {
                terminal.WriteError(chunk);
                if (IsLikelyInputPrompt(chunk)) {
                    awaitingInput = true;
                }
            },
            cwd
        );

        if (!outcome.has_value()) {
            return outcome;
        }

        terminal.WriteInfo("Sandbox process started. Type /stop in stdin prompt to terminate.");

        while (m_bridge.IsRunning()) {
            if (awaitingInput.exchange(false)) {
                auto userInput = terminal.ReadLine("zeri::sandbox::stdin> ");
                if (!userInput.has_value()) {
                    m_bridge.Terminate();
                    break;
                }

                if (*userInput == "/stop") {
                    m_bridge.Terminate();
                    break;
                }

                m_bridge.SendInput(*userInput);
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        const int exitCode = m_bridge.WaitForExit();
        if (exitCode != 0) {
            return std::unexpected(ExecutionError{
                "SANDBOX_RUN_FAILED",
                "Process exited with code: " + std::to_string(exitCode),
                executable.string(),
                { "Review stderr output shown above for diagnostics." }
            });
        }

        return Zeri::Engines::Success("[SandboxRun] Process completed successfully.");
    }

}

/*
SandboxContext.cpp — Module development and code execution environment.

Comandi disponibili:
  - /list: Fetches the manifest list from ModuleManager.
  - /build: Invokes cmake via ProcessBridge to compile a module.
  - /run: Esegue modulo o target esterno (file/eseguibile) in modalità bloccante.
  - /open: Opens a file/path in the configured IDE.
  - /set-ide: Sets the preferred IDE command.
  - /watch: Riporta lo stato runtime del processo sandbox.
  - /help: Formatted list of all sandbox commands.

Dipendenze: ProcessBridge, ModuleManager, RuntimeState.
*/
