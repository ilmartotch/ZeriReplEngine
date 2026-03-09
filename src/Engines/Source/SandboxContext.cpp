#include "../Include/SandboxContext.h"
#include "../../Core/Include/RuntimeState.h"
#include <algorithm>
#include <filesystem>
#include <format>

namespace fs = std::filesystem;

namespace Zeri::Engines::Defaults {

    void SandboxContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteLine("--- Sandbox Environment ---");
        terminal.WriteLine("Type /help to list sandbox commands.");
    }

    ExecutionOutcome SandboxContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (cmd.commandName == "list") return ListModules(state);

        if (cmd.commandName == "build" && !cmd.args.empty()) {
            return BuildModule(cmd.args[0], state, terminal);
        }

        if (cmd.commandName == "run" && !cmd.args.empty()) {
            return RunModule(cmd.args[0], state, terminal);
        }

        if (cmd.commandName == "help") {
            return
                "Sandbox Context Help\n"
                "--------------------\n"
                "Commands\n"
                "  /list\n"
                "  /build <moduleName>\n"
                "  /run <moduleName>\n"
                "\n"
                "Examples\n"
                "  /list\n"
                "  /build my_module\n"
                "  /run my_module\n"
                "  $sandbox | /list\n";
        }

        return std::unexpected(ExecutionError{ "SANDBOX_UNKNOWN", "Unknown command in sandbox." });
    }

    ExecutionOutcome SandboxContext::ListModules(Zeri::Core::RuntimeState& state) {
        auto modules = state.GetModuleManager().GetModules();
        if (modules.empty()) return "No modules found in 'modules/' directory.";

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

        // Build in the module's own directory, not the REPL root
        int eCode = m_bridge.ExecuteSync("cmake", { "--build", "." }, modulePath);
        if (eCode != 0) {
            return std::unexpected(ExecutionError{ "BUILD_FAIL", "Failed to build module (exit code: " + std::to_string(eCode) + ")." });
        }
        return "Build succeeded.";
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

        std::string capturedOutput;
        auto outcome = m_bridge.Run(entryPath, {}, [&](const std::string& chunk) {
            capturedOutput += chunk;
            terminal.Write(chunk);
        }, modulePath);

        if (!outcome.has_value()) {
            return outcome;
        }

        return capturedOutput.empty() ? "Module executed (no output)." : capturedOutput;
    }

}

/*
SandboxContext Implementation
Handles sandbox modules commands like /list, /build, and /run.
- /list: Fetches the manifest list from ModuleManager.
- /build: Invokes the system 'cmake' via std::system. This is a simple bridge to the compiler.
- /run: Uses ProcessBridge to execute the compiled binary and captures its output.
Note: For /run, the output is prefixed with the module name to distinguish it from REPL output.
*/
