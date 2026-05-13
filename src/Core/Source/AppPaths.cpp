#include "../Include/AppPaths.h"

#include <array>
#include <string>
#include <system_error>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
#else
    #include <unistd.h>
#endif

namespace Zeri::Core {

    std::optional<std::filesystem::path> TryResolveExecutablePath() {
#ifdef _WIN32
        std::wstring buffer(MAX_PATH, L'\0');
        const DWORD written = ::GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (written == 0 || written >= buffer.size()) {
            return std::nullopt;
        }
        buffer.resize(written);
        return std::filesystem::path(buffer);
#elif defined(__APPLE__)
        uint32_t size = 0;
        if (_NSGetExecutablePath(nullptr, &size) != -1 || size == 0) {
            return std::nullopt;
        }
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return std::nullopt;
        }
        std::error_code ec;
        auto canonicalPath = std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()), ec);
        if (ec) {
            return std::filesystem::path(buffer.c_str());
        }
        return canonicalPath;
#else
        std::array<char, 4096> buffer{};
        const ssize_t written = ::readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
        if (written <= 0) {
            return std::nullopt;
        }
        buffer[static_cast<size_t>(written)] = '\0';
        return std::filesystem::path(buffer.data());
#endif
    }

    std::filesystem::path ResolveExecutableDir() {
        if (const auto executablePath = TryResolveExecutablePath(); executablePath.has_value()) {
            const auto parent = executablePath->parent_path();
            if (!parent.empty()) {
                return parent;
            }
        }

        std::error_code ec;
        auto cwd = std::filesystem::current_path(ec);
        if (ec) {
            return std::filesystem::path(".");
        }
        return cwd;
    }

}

/*
AppPaths.cpp
Provides a single, reusable executable-directory resolution policy for Windows, macOS, and Linux.
Resource loaders should prefer ResolveExecutableDir() over process current_path().
*/
