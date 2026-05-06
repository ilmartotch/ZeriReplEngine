#pragma once

#include <filesystem>

namespace Zeri::Core {

    std::filesystem::path ResolveUserDataDir();
    std::filesystem::path ResolveSessionPath();
    std::filesystem::path ResolveSessionBackupPath();

}

/*
UserPaths exposes path resolution APIs for user-scoped persistent data.
The implementation is platform-aware and ensures session directories are created before use.
*/
