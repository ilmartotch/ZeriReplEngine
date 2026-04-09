#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <map>

namespace Zeri::Ui { class ITerminal; }

namespace Zeri::Core {

    struct ScriptRuntime {
        std::string language;
        std::string binary;
        bool available = false;
        bool hasJit = false;
    };

    struct SystemHealth {
        bool hasCMake = false;
        bool hasVcpkg = false;
        bool hasCompiler = false;
        std::string compilerName;
        std::vector<std::string> missingTools;
        std::vector<ScriptRuntime> runtimes;

        [[nodiscard]] const ScriptRuntime* GetRuntime(const std::string& language) const;

        [[nodiscard]] bool IsReady() const {
            return hasCMake && hasCompiler;
        }
    };

    class SystemGuard {
    public:
        [[nodiscard]] static SystemHealth CheckEnvironment();

        static void PrintGuide(const SystemHealth& health, Zeri::Ui::ITerminal& terminal);

        [[nodiscard]] static std::string GetInstallHint(std::string_view language);

    private:
        [[nodiscard]] static bool ProbeCommand(const std::string& cmd);
    };

}

/*
SystemGuard.h — Utility to check system requirements and guide the user.

Responsabilità:
  - CheckEnvironment(): Scans PATH for build tools (cmake, compilers) and
    scripting runtimes (lua, python, js, ruby). Returns SystemHealth.
  - PrintGuide(): Reports missing tools through the ITerminal abstraction.
  - GetInstallHint(): Returns a language-specific installation hint for
    user-facing diagnostics and ScriptHub runtime status.
  - ScriptRuntime: Holds language, binary path, availability and JIT status.
  - SystemHealth: Aggregates all probe results; IsReady() checks cmake + compiler.

Dipendenze: ITerminal (forward-declared).
*/
