#pragma once

#include "RuntimeState.h"
#include "StartupDiagnostics.h"

#include <expected>
#include <filesystem>
#include <string>
#include <vector>

namespace Zeri::Core {

    struct BugSnapshotCommandRecord {
        std::string command;
        std::string kind;
        std::string responseCode;
        bool success{ false };
    };

    struct BugSnapshotMetadata {
        std::string triggerCommand;
        std::vector<BugSnapshotCommandRecord> commandHistory;
    };

    struct BugSnapshotLimits {
        std::size_t maxFiles{ 10000 };
        std::size_t maxKeyFiles{ 1000 };
        std::size_t maxLogFiles{ 12 };
        std::size_t maxLogLinesPerFile{ 400 };
        std::size_t maxHashedBytesPerFile{ 8U * 1024U * 1024U };
        std::size_t maxLogBytesPerFile{ 512U * 1024U };
        std::size_t maxScanSeconds{ 20 };
    };

    [[nodiscard]] std::expected<std::filesystem::path, std::string> CreateBugSnapshot(
        const RuntimeState& runtimeState,
        const StartupDiagnosticsReport& startupDiagnostics,
        const std::filesystem::path& projectRoot,
        const BugSnapshotMetadata& metadata = {},
        const BugSnapshotLimits& limits = {}
    );

}

/*
BugSnapshot.h
Declares generation of safe bug-report snapshots saved under user data storage.
Snapshots include diagnostics, bounded project metadata scan, and bounded recent app logs.
*/
