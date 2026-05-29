#include "../Include/UserPaths.h"

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace {

    std::string ReadEnvVar(const char* name) {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
            return {};
        }

        std::string result(value);
        std::free(value);
        return result;
#else
        if (const char* value = std::getenv(name); value != nullptr) {
            return std::string(value);
        }
        return {};
#endif
    }

    std::optional<std::filesystem::path> TryResolveBaseUserDataDir() {
#if defined(_WIN32)
        if (const auto appData = ReadEnvVar("APPDATA"); !appData.empty()) {
            return std::filesystem::path(appData) / "Zeri";
        }
        if (const auto userProfile = ReadEnvVar("USERPROFILE"); !userProfile.empty()) {
            return std::filesystem::path(userProfile) / "AppData" / "Roaming" / "Zeri";
        }
#elif defined(__APPLE__)
        if (const auto home = ReadEnvVar("HOME"); !home.empty()) {
            return std::filesystem::path(home) / "Library" / "Application Support" / "zeri";
        }
#else
        if (const auto xdgConfigHome = ReadEnvVar("XDG_CONFIG_HOME"); !xdgConfigHome.empty()) {
            return std::filesystem::path(xdgConfigHome) / "zeri";
        }
        if (const auto home = ReadEnvVar("HOME"); !home.empty()) {
            return std::filesystem::path(home) / ".config" / "zeri";
        }
#endif

        return std::nullopt;
    }

}

namespace Zeri::Core {

    std::optional<std::filesystem::path> TryResolveUserDataDir() {
        return TryResolveBaseUserDataDir();
    }

    std::optional<std::filesystem::path> TryResolveScriptsDir() {
        if (const auto userDataDir = TryResolveBaseUserDataDir(); userDataDir.has_value()) {
            return *userDataDir / "scripts";
        }
        return std::nullopt;
    }

    std::optional<std::filesystem::path> TryResolveSessionsDir() {
        if (const auto userDataDir = TryResolveBaseUserDataDir(); userDataDir.has_value()) {
            return *userDataDir / "sessions";
        }
        return std::nullopt;
    }

    std::filesystem::path ResolveUserDataDir() {
        const auto maybeUserDataDir = TryResolveBaseUserDataDir();
        if (!maybeUserDataDir.has_value()) {
            throw std::runtime_error("Cannot resolve user data directory: no valid env var found");
        }

        const auto userDataDir = *maybeUserDataDir;
        std::filesystem::create_directories(userDataDir);
        return userDataDir;
    }

    std::filesystem::path ResolveScriptsDir() {
        const auto scriptsDir = ResolveUserDataDir() / "scripts";
        std::filesystem::create_directories(scriptsDir);
        return scriptsDir;
    }

    std::filesystem::path ResolveSessionsDir() {
        const auto sessionsDir = ResolveUserDataDir() / "sessions";
        std::filesystem::create_directories(sessionsDir);
        return sessionsDir;
    }

    std::filesystem::path ResolveSessionPath() {
        const auto sessionPath = ResolveSessionsDir() / "state.json";
        std::filesystem::create_directories(sessionPath.parent_path());
        return sessionPath;
    }

    std::filesystem::path ResolveSessionBackupPath() {
        const auto backupPath = ResolveSessionsDir() / "state.json.bak";
        std::filesystem::create_directories(backupPath.parent_path());
        return backupPath;
    }

}

/*
UserPaths centralizes user-scoped storage resolution across platforms.
ResolveUserDataDir selects APPDATA or USERPROFILE fallback on Windows,
HOME-based Application Support on macOS, and XDG_CONFIG_HOME or HOME fallback on Linux.
All exported functions guarantee parent directories exist before returning paths,
and raise a runtime_error when no required environment variable can be resolved.
*/
