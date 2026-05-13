#include "../Include/StartupDiagnostics.h"
#include "../Include/AppPaths.h"

#include <array>

namespace Zeri::Core {

    namespace {
        [[nodiscard]] bool ExistsNoThrow(const std::filesystem::path& path) {
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        }

        [[nodiscard]] std::filesystem::path ResolveRuntimeDir(const std::filesystem::path& executableDir) {
            const auto lower = executableDir / "runtime";
            if (ExistsNoThrow(lower)) {
                return lower;
            }

            const auto upper = executableDir / "Runtime";
            if (ExistsNoThrow(upper)) {
                return upper;
            }

            return lower;
        }
    }

    StartupDiagnosticsReport CollectStartupDiagnostics() {
        StartupDiagnosticsReport report;
        report.executableDir = ResolveExecutableDir();

        const auto helpCatalogPath = report.executableDir / "help" / "help_catalog.json";
        if (!ExistsNoThrow(helpCatalogPath)) {
            report.issues.push_back({
                "HELP_CATALOG_MISSING",
                "help/help_catalog.json not found next to executable.",
                "Ensure release packaging includes the help directory."
            });
        }

        const auto runtimeDir = ResolveRuntimeDir(report.executableDir);
        const auto runtimeManifestPath = runtimeDir / "runtime_manifest.json";
        if (!ExistsNoThrow(runtimeManifestPath)) {
            report.issues.push_back({
                "RUNTIME_MANIFEST_MISSING",
                "runtime_manifest.json not found next to executable runtime assets.",
                "Ensure release packaging includes runtime/runtime_manifest.json."
            });
        }

        const std::array<std::string_view, 4> runtimeScripts = {
            "bootstrap_bun.js",
            "bootstrap_lua.lua",
            "bootstrap_python.py",
            "bootstrap_ruby.rb"
        };
        for (const auto scriptName : runtimeScripts) {
            const auto scriptPath = runtimeDir / scriptName;
            if (ExistsNoThrow(scriptPath)) {
                continue;
            }

            report.issues.push_back({
                "RUNTIME_SCRIPT_MISSING",
                "Required runtime script missing: " + std::string(scriptName),
                "Ensure runtime bootstrap scripts are included in the release package."
            });
        }

        return report;
    }

}

/*
StartupDiagnostics.cpp
Collects startup asset diagnostics using no-throw filesystem checks and executable-relative paths.
*/
