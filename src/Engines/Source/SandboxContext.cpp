#include "../Include/SandboxContext.h"
#include "../../Core/Include/RuntimeState.h"
#include <filesystem>
#include <format>

namespace fs = std::filesystem;

namespace Zeri::Engines::Defaults {

    void SandboxContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteLine("--- Sandbox Environment ---");
        terminal.WriteLine("Use /list to see modules, /build <name> to compile, /run <name> to execute.");
    }

    ExecutionOutcome SandboxContext::HandleCommand(
        const std::string& commandName,
        const std::vector<std::string>& args,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        if (commandName == "list") return ListModules(state);
        
        if (commandName == "build" && !args.empty()) {
            return BuildModule(args[0], state, terminal);
        }

        if (commandName == "run" && !args.empty()) {
            return RunModule(args[0], state, terminal);
        }

        if (commandName == "help") {
            return "Sandbox Commands: /list, /build <name>, /run <name>, /back";
        }

        return std::unexpected(ExecutionError{"SANDBOX_UNKNOWN", "Unknown command in sandbox."});
    }

    ExecutionOutcome SandboxContext::ListModules(Zeri::Core::RuntimeState& state) {
        auto modules = state.GetModuleManager().GetModules();
        if (modules.empty()) return "No modules found in 'modules/' directory.";

        std::string result = "Available Modules:";
        for (const auto& m : modules) {
            result += std::format("- {} (v{}) [{}]\n", m.name, m.version, m.type);
        }
        return result;
    }

    ExecutionOutcome SandboxContext::BuildModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal) {
        auto modules = state.GetModuleManager().GetModules();
        auto it = std::find_if(modules.begin(), modules.end(), [&](const auto& m) { return m.name == moduleName; });

        if (it == modules.end()) return std::unexpected(ExecutionError{"NOT_FOUND", "Module not found."});

        terminal.WriteLine(std::format("Configuring and building: {}...", moduleName));
        
        std::string modulePath = it->path;
        // Simple CMake automation
        std::string configCmd = std::format("cmake -S \"{}\" -B \"{}/build\"", modulePath, modulePath);
        std::string buildCmd = std::format("cmake --build \"{}/build\"", modulePath);

        terminal.WriteLine("Running CMake configure...");
        int res = std::system(configCmd.c_str());
        if (res != 0) return std::unexpected(ExecutionError{"BUILD_ERR", "CMake configuration failed."});

        terminal.WriteLine("Running CMake build...");
        res = std::system(buildCmd.c_str());
        if (res != 0) return std::unexpected(ExecutionError{"BUILD_ERR", "CMake build failed."});

        return "Build successful.";
    }

    ExecutionOutcome SandboxContext::RunModule(const std::string& moduleName, Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal) {
        auto modules = state.GetModuleManager().GetModules();
        auto it = std::find_if(modules.begin(), modules.end(), [&](const auto& m) { return m.name == moduleName; });

        if (it == modules.end()) return std::unexpected(ExecutionError{"NOT_FOUND", "Module not found."});

        // Path to executable (assuming default CMake output for MVP)
        #ifdef _WIN32
        // Usa _dupenv_s per ottenere la variabile d'ambiente in modo sicuro
            char* osEnv = nullptr;
            size_t len = 0;
            _dupenv_s(&osEnv, &len, "OS");
            bool isWindows = (osEnv != nullptr);
            if (osEnv) free(osEnv);
            fs::path exePath = fs::path(it->path) / "build" / (it->name + (isWindows ? ".exe" : ""));
        #else
            fs::path exePath = fs::path(it->path) / "build" / (it->name);
        #endif

        if (!fs::exists(exePath)) {
            terminal.WriteLine("Executable not found. Attempting build first...");
            auto buildRes = BuildModule(moduleName, state, terminal);
            if (!buildRes) return buildRes;
        }

        terminal.WriteLine(std::format("--- Starting {} ---", moduleName));
        
        auto outcome = m_bridge.Run(exePath.string(), {}, [&](const std::string& output) {
            terminal.Write(std::format("[{}] {}", moduleName, output));
        });

        // Loop to allow basic interaction while process is running
        while (m_bridge.IsRunning()) {
            // This is a simple pass-through. In a full implementation, 
            // we'd use a non-blocking ReadLine or a separate thread for UI.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        return std::format("--- {} Finished ---", moduleName);
    }

}

/*
FILE DOCUMENTATION:
SandboxContext Implementation.
Commands:
- /list: Fetches the manifest list from ModuleManager.
- /build: Invokes the system 'cmake' via std::system. This is a simple bridge to the compiler.
- /run: Uses ProcessBridge to execute the compiled binary and captures its output.
Note: For /run, the output is prefixed with the module name to distinguish it from REPL output.
*/
