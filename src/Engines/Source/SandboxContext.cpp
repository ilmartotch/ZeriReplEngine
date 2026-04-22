#include "../Include/SandboxContext.h"
#include "../../Core/Include/RuntimeState.h"
#include "../../Core/Include/SystemGuard.h"
#include <nlohmann/json.hpp>
#include <any>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <format>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string_view>
#include <thread>

#ifdef _WIN32
    #include <Windows.h>
#endif

namespace fs = std::filesystem;

namespace {

    enum class SandboxLanguage {
        Unknown,
        Python,
        Lua,
        JavaScript,
        TypeScript,
        Ruby
    };

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

    [[nodiscard]] std::string_view Trim(std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
        return value;
    }

    [[nodiscard]] std::string NormalizePathInput(std::string_view value) {
        value = Trim(value);
        if (value.size() >= 2) {
            const char first = value.front();
            const char last = value.back();
            if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
                value.remove_prefix(1);
                value.remove_suffix(1);
            }
        }

        return std::string(Trim(value));
    }

    [[nodiscard]] SandboxLanguage DetectLanguage(std::string_view filePath) {
        const std::string extension = ToLower(fs::path(filePath).extension().string());
        if (extension == ".py") return SandboxLanguage::Python;
        if (extension == ".lua") return SandboxLanguage::Lua;
        if (extension == ".js") return SandboxLanguage::JavaScript;
        if (extension == ".ts") return SandboxLanguage::TypeScript;
        if (extension == ".rb") return SandboxLanguage::Ruby;
        return SandboxLanguage::Unknown;
    }

    [[nodiscard]] std::string RuntimeKey(SandboxLanguage language) {
        if (language == SandboxLanguage::Python) return "python";
        if (language == SandboxLanguage::Lua) return "lua";
        if (language == SandboxLanguage::JavaScript) return "js";
        if (language == SandboxLanguage::TypeScript) return "js";
        if (language == SandboxLanguage::Ruby) return "ruby";
        return {};
    }

    [[nodiscard]] std::string ExtensionOrPlaceholder(const fs::path& filePath) {
        const auto extension = filePath.extension().string();
        if (!extension.empty()) {
            return extension;
        }
        return "<none>";
    }

    [[nodiscard]] fs::path BuildSessionTempRoot() {
#ifdef _WIN32
        char* value = nullptr;
        size_t len = 0;
        if (_dupenv_s(&value, &len, "ZERI_SESSION_TEMP_DIR") == 0 && value != nullptr) {
            std::string fromEnv(value);
            std::free(value);
            if (!fromEnv.empty()) {
                return fs::path(fromEnv);
            }
        }
#else
        const char* raw = std::getenv("ZERI_SESSION_TEMP_DIR");
        if (raw != nullptr) {
            const std::string fromEnv(raw);
            if (!fromEnv.empty()) {
                return fs::path(fromEnv);
            }
        }
#endif

        const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const auto fallback = std::format("session-{}", timestamp);
        return fs::temp_directory_path() / "zeri-project" / fallback;
    }

    [[nodiscard]] fs::path SessionTempRoot() {
        static const fs::path root = [] {
            std::error_code ec;
            fs::path base = BuildSessionTempRoot();
            fs::create_directories(base, ec);
            return base;
        }();
        return root;
    }

    [[nodiscard]] fs::path SandboxWorkspaceRoot() {
        static const fs::path root = [] {
            std::error_code ec;
            fs::path path = SessionTempRoot() / "sandbox";
            fs::create_directories(path, ec);
            return path;
        }();
        return root;
    }

    [[nodiscard]] std::string BuildArtifactStamp() {
        const auto now = std::chrono::system_clock::now();
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
        const std::time_t timeNow = std::chrono::system_clock::to_time_t(now);

        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &timeNow);
#else
        localtime_r(&timeNow, &local);
#endif

        std::ostringstream stamp;
        stamp << std::put_time(&local, "%Y%m%d_%H%M%S")
              << '_' << std::setw(3) << std::setfill('0') << millis.count();
        return stamp.str();
    }

    void PersistExecutionArtifacts(
        const fs::path& workspaceRoot,
        const fs::path& executable,
        const std::vector<std::string>& args,
        const std::optional<fs::path>& cwd,
        const std::string& stdoutData,
        const std::string& stderrData,
        int exitCode,
        bool interruptedByUser
    ) {
        std::error_code ec;
        fs::create_directories(workspaceRoot, ec);

        const std::string stamp = BuildArtifactStamp();
        const fs::path logPath = workspaceRoot / std::format("exec_{}.log", stamp);
        const fs::path jsonPath = workspaceRoot / std::format("exec_{}.json", stamp);

        {
            std::ofstream logStream(logPath, std::ios::binary | std::ios::trunc);
            if (logStream.is_open()) {
                logStream << "[stdout]\n";
                logStream << stdoutData;
                if (!stdoutData.empty() && stdoutData.back() != '\n') {
                    logStream << '\n';
                }
                logStream << "\n[stderr]\n";
                logStream << stderrData;
                if (!stderrData.empty() && stderrData.back() != '\n') {
                    logStream << '\n';
                }
            }
        }

        nlohmann::json manifest;
        manifest["timestamp"] = stamp;
        manifest["executable"] = executable.string();
        manifest["arguments"] = args;
        manifest["workingDirectory"] = cwd.has_value() ? cwd->string() : "";
        manifest["exitCode"] = exitCode;
        manifest["interrupted"] = interruptedByUser;
        manifest["stdoutBytes"] = stdoutData.size();
        manifest["stderrBytes"] = stderrData.size();
        manifest["logFile"] = logPath.string();

        std::ofstream jsonStream(jsonPath, std::ios::binary | std::ios::trunc);
        if (jsonStream.is_open()) {
            jsonStream << manifest.dump(2);
        }
    }

}

namespace Zeri::Engines::Defaults {

    void SandboxContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("Sandbox environment active - paste a file path to execute it, or type /help for commands.");
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
        if (cmd.type == InputType::Expression) {
            const std::string rawPath = cmd.args.empty() ? cmd.rawInput : cmd.args.front();
            return RunExternalFilePath(rawPath, cmd, terminal);
        }

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
                "Sandbox Context - Available Commands\n"
                "\n"
                "Global Commands:\n"
                "  /help - Show help for the active context\n"
                "  /context - List available contexts\n"
                "  /back - Return to previous context\n"
                "  /save - Save session state to disk\n"
                "  /clear - Clear the chat history\n"
                "  /status - Show engine diagnostics\n"
                "  /reset - Reset the current session\n"
                "  /exit - Exit the REPL\n"
                "\n"
                "Module Commands:\n"
                "  /list - List all available modules\n"
                "  /build <moduleName> - Build a module using CMake\n"
                "  /run <moduleName|filePath> [args...] [--cwd <path>] - Run module or external target\n"
                "  <filePath> - Execute an external script file by path (.py .lua .js .ts .rb)\n"
                "\n"
                "IDE + Monitoring:\n"
                "  /open [file] - Open file/path in configured IDE\n"
                "  /set-ide <name> - Set preferred IDE command\n"
                "  /watch - Show current sandbox process status\n"
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

    ExecutionOutcome SandboxContext::RunExternalFilePath(
        std::string_view filePathInput,
        const Command& origin,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string normalizedPath = NormalizePathInput(filePathInput);
        if (normalizedPath.empty()) {
            return std::unexpected(ExecutionError{
                "SANDBOX_MISSING_ARGS",
                "Missing file path for sandbox execution.",
                origin.rawInput,
                { "Provide a valid file path, for example: /tmp/script.py" }
            });
        }

        fs::path filePath = fs::path(normalizedPath);
        if (filePath.is_relative()) {
            filePath = fs::absolute(filePath);
        }

        if (!fs::exists(filePath)) {
            return std::unexpected(ExecutionError{
                "SANDBOX_TARGET_NOT_FOUND",
                "File not found: " + filePath.string(),
                origin.rawInput
            });
        }

        if (!fs::is_regular_file(filePath)) {
            return std::unexpected(ExecutionError{
                "SANDBOX_INVALID_TARGET",
                "Path does not point to a file: " + filePath.string(),
                origin.rawInput
            });
        }

        std::ifstream readable(filePath, std::ios::in);
        if (!readable.good()) {
            return std::unexpected(ExecutionError{
                "SANDBOX_FILE_NOT_READABLE",
                "File is not readable: " + filePath.string(),
                origin.rawInput
            });
        }

        const SandboxLanguage language = DetectLanguage(filePath.string());
        if (language == SandboxLanguage::Unknown) {
            return std::unexpected(ExecutionError{
                "SANDBOX_UNSUPPORTED_EXTENSION",
                "Unsupported file extension: " + ExtensionOrPlaceholder(filePath),
                origin.rawInput,
                { "Supported extensions: .py .lua .js .ts .rb" }
            });
        }

        auto health = Zeri::Core::SystemGuard::CheckEnvironment();
        const std::string runtimeKey = RuntimeKey(language);
        const auto* runtime = health.GetRuntime(runtimeKey);
        if (runtime == nullptr || !runtime->available) {
            return std::unexpected(ExecutionError{
                "SANDBOX_RUNTIME_MISSING",
                "No runtime available for extension: " + ExtensionOrPlaceholder(filePath),
                origin.rawInput,
                { "Use /status in global context to inspect runtime availability." }
            });
        }

        terminal.WriteInfo("Running external file: " + filePath.filename().string());
        return RunBlockingExternal(runtime->binary, { filePath.string() }, SandboxWorkspaceRoot(), terminal);
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
                    workingDirectory = SandboxWorkspaceRoot();
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
        bool interruptedByUser = false;
        std::mutex captureMutex;
        std::string capturedStdout;
        std::string capturedStderr;

        terminal.WriteInfo("Sandbox process status: running");

        auto outcome = m_bridge.Run(
            executable,
            args,
            [&](const std::string& chunk) {
                {
                    std::lock_guard lock(captureMutex);
                    capturedStdout += chunk;
                }
                terminal.Write(chunk);
                if (IsLikelyInputPrompt(chunk)) {
                    awaitingInput = true;
                }
            },
            [&](const std::string& chunk) {
                {
                    std::lock_guard lock(captureMutex);
                    capturedStderr += chunk;
                }
                terminal.WriteError(chunk);
                if (IsLikelyInputPrompt(chunk)) {
                    awaitingInput = true;
                }
            },
            cwd
        );

        if (!outcome.has_value()) {
            terminal.WriteInfo("Sandbox process status: idle");
            return outcome;
        }

        terminal.WriteInfo("Sandbox process started. Type /stop in stdin prompt to interrupt execution.");

        while (m_bridge.IsRunning()) {
            if (awaitingInput.exchange(false)) {
                auto userInput = terminal.ReadLine("zeri::sandbox::stdin> ");
                if (!userInput.has_value()) {
                    interruptedByUser = true;
                    m_bridge.Terminate();
                    break;
                }

                if (*userInput == "/stop") {
                    interruptedByUser = true;
                    m_bridge.Terminate();
                    break;
                }

                m_bridge.SendInput(*userInput);
                continue;
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }

        const int exitCode = m_bridge.WaitForExit();

        {
            std::lock_guard lock(captureMutex);
            PersistExecutionArtifacts(
                SandboxWorkspaceRoot(),
                executable,
                args,
                cwd,
                capturedStdout,
                capturedStderr,
                exitCode,
                interruptedByUser
            );
        }

        terminal.WriteInfo("Sandbox process status: idle");

        if (interruptedByUser) {
            return Zeri::Engines::Warning("Sandbox process interrupted by user.");
        }

        if (exitCode != 0) {
            return std::unexpected(ExecutionError{
                "SANDBOX_RUN_FAILED",
                "Sandbox process terminated with exit code: " + std::to_string(exitCode),
                executable.string(),
                { "Review stderr output shown above for diagnostics.",
                  "If execution was intentional, use /stop when prompted for stdin." }
            });
        }

        return Zeri::Engines::Success("Sandbox process completed successfully.");
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
