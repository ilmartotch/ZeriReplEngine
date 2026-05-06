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

    std::filesystem::path ResolveBaseUserDataDir() {
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

        throw std::runtime_error("Cannot resolve user data directory: no valid env var found");
    }

}

namespace Zeri::Core {

    std::filesystem::path ResolveUserDataDir() {
        const auto userDataDir = ResolveBaseUserDataDir();
        std::filesystem::create_directories(userDataDir);
        return userDataDir;
    }

    std::filesystem::path ResolveSessionPath() {
        const auto sessionPath = ResolveUserDataDir() / "sessions" / "state.json";
        std::filesystem::create_directories(sessionPath.parent_path());
        return sessionPath;
    }

    std::filesystem::path ResolveSessionBackupPath() {
        const auto backupPath = ResolveUserDataDir() / "sessions" / "state.json.bak";
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
