#pragma once

#include <filesystem>
#include <optional>

namespace Zeri::Core {

    std::optional<std::filesystem::path> TryResolveUserDataDir();
    std::optional<std::filesystem::path> TryResolveScriptsDir();
    std::optional<std::filesystem::path> TryResolveSessionsDir();

    std::filesystem::path ResolveUserDataDir();
    std::filesystem::path ResolveScriptsDir();
    std::filesystem::path ResolveSessionsDir();
    std::filesystem::path ResolveSessionPath();
    std::filesystem::path ResolveSessionBackupPath();

}

/*
UserPaths exposes path resolution APIs for user-scoped persistent data.
The implementation is platform-aware and ensures session directories are created before use.
*/
