#pragma once

#include <filesystem>
#include <optional>

namespace Zeri::Core {

    [[nodiscard]] std::optional<std::filesystem::path> TryResolveExecutablePath();
    [[nodiscard]] std::filesystem::path ResolveExecutableDir();

}

/*
AppPaths.h
Declares cross-platform executable path helpers used by startup diagnostics and resource resolution.
*/
