#include "../Include/SystemGuard.h"
#include "../../Ui/Include/ITerminal.h"
#include <array>
#include <memory>
#include <cstdio>

namespace Zeri::Core {

    SystemHealth SystemGuard::CheckEnvironment() {
        SystemHealth health;

        health.hasCMake = ProbeCommand("cmake --version");
        health.hasVcpkg = ProbeCommand("vcpkg --version");

        #ifdef _WIN32
            health.hasCompiler = ProbeCommand("cl") || ProbeCommand("g++ --version");
            health.compilerName = health.hasCompiler ? "MSVC/MinGW" : "None";
        #else
            health.hasCompiler = ProbeCommand("g++ --version") || ProbeCommand("clang++ --version");
            health.compilerName = health.hasCompiler ? "GCC/Clang" : "None";
        #endif

        if (!health.hasCMake) health.missingTools.push_back("CMake");
        if (!health.hasCompiler) health.missingTools.push_back("C++ Compiler (MSVC, GCC, or Clang)");

        return health;
    }

    bool SystemGuard::ProbeCommand(const std::string& cmd) {
        #ifdef _WIN32
            std::string fullCmd = cmd + " > nul 2>&1";
        #else
            std::string fullCmd = cmd + " > /dev/null 2>&1";
        #endif
            return std::system(fullCmd.c_str()) == 0;
    }

    void SystemGuard::PrintGuide(const SystemHealth& health, Zeri::Ui::ITerminal& terminal) {
        if (health.IsReady()) return;

        terminal.WriteError("SYSTEM CHECK FAILED");
        for (const auto& tool : health.missingTools) {
            terminal.WriteLine(" - Missing: " + tool);
        }
        terminal.WriteLine("To use the Sandbox features, please install the missing tools.");
        terminal.WriteLine("Visit: https://cmake.org/download/ to get CMake.");
    }

}

/*
FILE DOCUMENTATION:
SystemGuard Implementation.
Uses std::system to 'probe' for commands by checking their exit codes.
Redirects output to null to keep the REPL interface clean during the check.
*/
