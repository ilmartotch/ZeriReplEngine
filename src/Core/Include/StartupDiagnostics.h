#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Zeri::Core {

    struct StartupIssue {
        std::string code;
        std::string message;
        std::string hint;
    };

    struct StartupDiagnosticsReport {
        std::filesystem::path executableDir;
        std::vector<StartupIssue> issues;
    };

    [[nodiscard]] StartupDiagnosticsReport CollectStartupDiagnostics();

}

/*
StartupDiagnostics.h
Declares startup asset checks used to surface deterministic diagnostics before runtime command handling.
*/
