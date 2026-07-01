#include "../Include/StartupDiagnostics.h"
#include "../Include/AppPaths.h"

#include <algorithm>
#include <array>
#include <vector>

namespace Zeri::Core {

    namespace {
        [[nodiscard]] bool ExistsNoThrow(const std::filesystem::path& path) {
            std::error_code ec;
            return std::filesystem::exists(path, ec) && !ec;
        }

        void AppendUniquePath(
            std::vector<std::filesystem::path>& out,
            const std::filesystem::path& path
        ) {
            if (path.empty()) {
                return;
            }

            const auto alreadyPresent = std::ranges::find(out, path) != out.end();
            if (!alreadyPresent) {
                out.push_back(path);
            }
        }

        void AppendAncestors(std::vector<std::filesystem::path>& out, std::filesystem::path start) {
            std::error_code ec;
            while (!start.empty()) {
                AppendUniquePath(out, start);
                const auto parent = start.parent_path();
                if (parent == start) {
                    break;
                }
                start = parent;
                ec.clear();
            }
        }

        [[nodiscard]] std::vector<std::filesystem::path> ResolveSearchRoots(const std::filesystem::path& executableDir) {
            std::vector<std::filesystem::path> roots;
            AppendAncestors(roots, executableDir);

            std::error_code cwdEc;
            const auto cwd = std::filesystem::current_path(cwdEc);
            if (!cwdEc) {
                AppendAncestors(roots, cwd);
            }

            return roots;
        }

        [[nodiscard]] bool HasRuntimeManifest(const std::vector<std::filesystem::path>& roots) {
            for (const auto& root : roots) {
                if (ExistsNoThrow(root / "runtime" / "runtime_manifest.json")) {
                    return true;
                }
                if (ExistsNoThrow(root / "Runtime" / "runtime_manifest.json")) {
                    return true;
                }
            }
            return false;
        }

        [[nodiscard]] bool HasRuntimeScript(
            const std::vector<std::filesystem::path>& roots,
            std::string_view scriptName
        ) {
            for (const auto& root : roots) {
                if (ExistsNoThrow(root / "runtime" / scriptName)) {
                    return true;
                }
                if (ExistsNoThrow(root / "Runtime" / scriptName)) {
                    return true;
                }
            }
            return false;
        }
    }

    StartupDiagnosticsReport CollectStartupDiagnostics() {
        StartupDiagnosticsReport report;
        report.executableDir = ResolveExecutableDir();
        const auto searchRoots = ResolveSearchRoots(report.executableDir);

        if (!HasRuntimeManifest(searchRoots)) {
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
            if (HasRuntimeScript(searchRoots, scriptName)) {
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
