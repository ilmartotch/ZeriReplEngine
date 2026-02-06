#pragma once

#include <string>
#include <vector>
#include <map>

namespace Zeri::Core {

    struct SystemHealth {
        bool hasCMake = false;
        bool hasVcpkg = false;
        bool hasCompiler = false;
        std::string compilerName;
        std::vector<std::string> missingTools;

        [[nodiscard]] bool IsReady() const {
            return hasCMake && hasCompiler;
        }
    };

    /**
     * @brief Utility to check system requirements and guide the user.
     */
    class SystemGuard {
    public:
        [[nodiscard]] static SystemHealth CheckEnvironment();
        
        // Suggests actions based on missing tools
        static void PrintGuide(const SystemHealth& health);

    private:
        [[nodiscard]] static bool ProbeCommand(const std::string& cmd);
    };

}

/*
FILE DOCUMENTATION:
SystemGuard Header.
Provides static methods to verify if CMake and compilers are available in the system PATH.
This is crucial for the Sandbox execution model where we rely on external build tools.
*/
