#include "../Include/SystemGuard.h"
#include "../../Ui/Include/ITerminal.h"
#include <array>
#include <cctype>
#include <cstdlib>
#include <memory>
#include <cstdio>
#include <optional>
#include <sstream>
#include <string_view>

#ifdef _WIN32
    #include <stdio.h>
#endif

namespace Zeri::Core {

    namespace {
        [[nodiscard]] std::string Trim(std::string value) {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
                value.erase(value.begin());
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
                value.pop_back();
            }
            return value;
        }

        [[nodiscard]] std::string ToLower(std::string_view value) {
            std::string out;
            out.reserve(value.size());
            for (char c : value) {
                out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return out;
        }

        [[nodiscard]] std::string ReadFirstLine(std::string_view text) {
            std::istringstream stream{ std::string(text) };
            std::string line;
            while (std::getline(stream, line)) {
                line = Trim(std::move(line));
                if (!line.empty()) {
                    return line;
                }
            }
            return {};
        }

        [[nodiscard]] std::optional<std::string> RunCommandCapture(const std::string& command) {
#ifdef _WIN32
            std::unique_ptr<FILE, decltype(&_pclose)> pipe(_popen(command.c_str(), "r"), _pclose);
#else
            std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
#endif
            if (!pipe) {
                return std::nullopt;
            }

            std::array<char, 512> buffer{};
            std::string output;
            while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
                output += buffer.data();
            }
            return output;
        }

        [[nodiscard]] std::optional<std::string> ResolveCommandPath(const std::string& binary) {
#ifdef _WIN32
            const std::string command = "where " + binary + " 2>nul";
#else
            const std::string command = "which " + binary + " 2>/dev/null";
#endif
            auto output = RunCommandCapture(command);
            if (!output.has_value()) {
                return std::nullopt;
            }

            std::string line = ReadFirstLine(output.value());
            if (line.empty()) {
                return std::nullopt;
            }
            return line;
        }

        [[nodiscard]] bool ParseMajorMinor(std::string_view value, int& major, int& minor) {
            const size_t firstDigit = value.find_first_of("0123456789");
            if (firstDigit == std::string_view::npos) {
                return false;
            }

            size_t idx = firstDigit;
            int parsedMajor = 0;
            while (idx < value.size() && std::isdigit(static_cast<unsigned char>(value[idx])) != 0) {
                parsedMajor = parsedMajor * 10 + (value[idx] - '0');
                ++idx;
            }

            if (idx >= value.size() || value[idx] != '.') {
                return false;
            }
            ++idx;

            if (idx >= value.size() || std::isdigit(static_cast<unsigned char>(value[idx])) == 0) {
                return false;
            }

            int parsedMinor = 0;
            while (idx < value.size() && std::isdigit(static_cast<unsigned char>(value[idx])) != 0) {
                parsedMinor = parsedMinor * 10 + (value[idx] - '0');
                ++idx;
            }

            major = parsedMajor;
            minor = parsedMinor;
            return true;
        }

        [[nodiscard]] bool DetectRubyJit(std::string_view rubyBinaryPath) {
            std::string command = "\"" + std::string(rubyBinaryPath) + "\" --version";
#ifdef _WIN32
            command += " 2>nul";
#else
            command += " 2>/dev/null";
#endif
            const auto output = RunCommandCapture(command);
            if (!output.has_value()) {
                return false;
            }

            int major = 0;
            int minor = 0;
            if (!ParseMajorMinor(output.value(), major, minor)) {
                return false;
            }
            return major > 3 || (major == 3 && minor >= 3);
        }

    }

    const ScriptRuntime* SystemHealth::GetRuntime(const std::string& language) const {
        const std::string requested = ToLower(language);
        for (const auto& runtime : runtimes) {
            if (ToLower(runtime.language) == requested) {
                return &runtime;
            }
        }
        return nullptr;
    }

    std::string SystemGuard::GetInstallHint(std::string_view language) {
        const std::string key = ToLower(language);
        if (key == "lua") {
            return "Install luajit and ensure it is available in PATH.";
        }
        if (key == "python") {
            return "Install python3 and ensure it is available in PATH.";
        }
        if (key == "js" || key == "ts") {
            return "Install bun and ensure it is available in PATH.";
        }
        if (key == "ruby") {
            return "Install Ruby 3.3+ (YJIT) and ensure it is available in PATH.";
        }
        return "Install a compatible runtime and ensure it is available in PATH.";
    }

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

        auto addRuntime = [&](std::string language, const std::vector<std::string>& chain, bool detectRubyJitFlag = false, std::string jitBinary = {}) {
            ScriptRuntime runtime;
            runtime.language = std::move(language);

            for (const auto& candidate : chain) {
                const auto resolved = ResolveCommandPath(candidate);
                if (!resolved.has_value()) {
                    continue;
                }

                runtime.available = true;
                runtime.binary = resolved.value();
                if (detectRubyJitFlag) {
                    runtime.hasJit = DetectRubyJit(runtime.binary);
                } else if (!jitBinary.empty()) {
                    runtime.hasJit = (candidate == jitBinary);
                }
                break;
            }

            if (!runtime.available) {
                health.missingTools.push_back("[WARN] Runtime '" + runtime.language + "' not found.");
            }

            health.runtimes.push_back(std::move(runtime));
        };

        addRuntime("lua", { "luajit" }, false, "luajit");
        addRuntime("python", { "python3", "python" }, false, "python3");
        addRuntime("js", { "bun" });
        addRuntime("ruby", { "ruby" }, true);

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
        if (!health.IsReady()) {
            terminal.WriteError("SYSTEM CHECK FAILED");
            for (const auto& tool : health.missingTools) {
                if (tool.starts_with("[WARN]")) {
                    continue;
                }
                terminal.WriteLine(" - Missing: " + tool);
            }
            terminal.WriteLine("To use the Sandbox features, please install the missing tools.");
            terminal.WriteLine("Visit: https://cmake.org/download/ to get CMake.");
        }

        bool hasRuntimeWarnings = false;
        for (const auto& runtime : health.runtimes) {
            if (!runtime.available) {
                hasRuntimeWarnings = true;
                break;
            }
        }

        if (!hasRuntimeWarnings) {
            return;
        }

        terminal.WriteInfo("Optional scripting runtimes not available:");
        for (const auto& runtime : health.runtimes) {
            if (runtime.available) {
                continue;
            }
            terminal.WriteLine("[WARN] Runtime '" + runtime.language + "' not found in Zeri environment.");
            terminal.WriteLine("       -> " + GetInstallHint(runtime.language));
        }
    }

}

/*
SystemGuard.cpp — Implementation of system environment diagnostics.

CheckEnvironment():
  Probes PATH for cmake, vcpkg, compilers (MSVC/GCC/Clang) and scripting
  runtimes (lua, python, js, ruby). Each runtime is resolved with a
  single standard binary (luajit, python3, bun, ruby).
  JIT detection remains active for ruby via version parsing.

GetInstallHint():
  Returns language-specific setup guidance used by diagnostics and ScriptHub.

PrintGuide():
  Reports missing build tools via WriteError and missing optional runtimes
  via WriteInfo with installation hints.

Dipendenze: ITerminal (forward-declared).

QA Changes:
  - Rimossi runtime cpp (cling) e rust (evcxr): non previsti in questa versione.
  - Rimossi RuntimeHint per cpp e rust.
*/
