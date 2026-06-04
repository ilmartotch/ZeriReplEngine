#pragma once

#include <cstdlib>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Zeri::Engines::Utils {

    namespace detail {
        [[nodiscard]] inline std::string ReadEnvVar(const char* key) {
#ifdef _WIN32
            char* valueRaw = nullptr;
            size_t valueLength = 0;
            const errno_t error = _dupenv_s(&valueRaw, &valueLength, key);
            if (error == 0 && valueRaw != nullptr && valueLength > 1) {
                std::string value(valueRaw);
                std::free(valueRaw);
                return value;
            }
            if (valueRaw != nullptr) {
                std::free(valueRaw);
            }
            return {};
#else
            const char* valueRaw = std::getenv(key);
            if (valueRaw == nullptr || *valueRaw == '\0') {
                return {};
            }
            return std::string(valueRaw);
#endif
        }

        [[nodiscard]] inline std::vector<std::string> SplitByDelimiter(std::string_view text, char delimiter) {
            std::vector<std::string> values;
            size_t start = 0;
            while (start <= text.size()) {
                const size_t end = text.find(delimiter, start);
                if (end == std::string_view::npos) {
                    values.emplace_back(text.substr(start));
                    break;
                }
                values.emplace_back(text.substr(start, end - start));
                start = end + 1;
            }
            return values;
        }

        [[nodiscard]] inline std::vector<std::string> ResolvePathExtensions() {
#ifdef _WIN32
            std::vector<std::string> extensions;
            const std::string pathExtValue = ReadEnvVar("PATHEXT");
            const std::string_view pathExt = pathExtValue.empty()
                ? std::string_view(".COM;.EXE;.BAT;.CMD")
                : std::string_view(pathExtValue);
            for (const auto& item : SplitByDelimiter(pathExt, ';')) {
                if (!item.empty()) {
                    extensions.push_back(item);
                }
            }
            return extensions;
#else
            return {};
#endif
        }

        [[nodiscard]] inline bool IsPathCandidate(std::string_view candidate) {
            return candidate.contains('/') || candidate.contains('\\') || candidate.starts_with(".");
        }

        [[nodiscard]] inline bool Exists(const std::filesystem::path& path) {
            std::error_code ec;
            return std::filesystem::exists(path, ec);
        }
    }

    [[nodiscard]] inline std::string ResolveExecutable(const std::vector<std::string>& candidates) {
        std::vector<std::string> orderedCandidates = candidates;
        for (size_t i = 0; i < orderedCandidates.size(); ++i) {
            if (orderedCandidates[i] != "python") {
                continue;
            }

            bool hasPython3Before = false;
            for (size_t j = 0; j < i; ++j) {
                if (orderedCandidates[j] == "python3") {
                    hasPython3Before = true;
                    break;
                }
            }
            if (!hasPython3Before) {
                orderedCandidates.insert(orderedCandidates.begin() + i, "python3");
            }
            break;
        }

        const std::string pathValue = detail::ReadEnvVar("PATH");
        const std::string_view pathText(pathValue);
#ifdef _WIN32
        const char pathSeparator = ';';
#else
        const char pathSeparator = ':';
#endif

        const auto pathEntries = detail::SplitByDelimiter(pathText, pathSeparator);
        const auto pathExtensions = detail::ResolvePathExtensions();

        std::string firstNonEmpty;
        for (const auto& candidate : orderedCandidates) {
            if (candidate.empty()) {
                continue;
            }

            if (firstNonEmpty.empty()) {
                firstNonEmpty = candidate;
            }

            const std::filesystem::path candidatePath(candidate);
            if (detail::Exists(candidatePath)) {
                return candidate;
            }

            if (detail::IsPathCandidate(candidate)) {
                continue;
            }

            for (const auto& entry : pathEntries) {
                if (entry.empty()) {
                    continue;
                }

                const std::filesystem::path basePath = std::filesystem::path(entry) / candidate;
                if (detail::Exists(basePath)) {
                    return basePath.string();
                }

#ifdef _WIN32
                if (!basePath.has_extension()) {
                    for (const auto& extension : pathExtensions) {
                        const std::filesystem::path withExt = std::filesystem::path(basePath.string() + extension);
                        if (detail::Exists(withExt)) {
                            return withExt.string();
                        }
                    }
                }
#endif
            }
        }

        return firstNonEmpty;
    }

    [[nodiscard]] inline std::filesystem::path ResolveBootstrapPath(const std::string& engineDir, const std::string& scriptName) {
        const std::filesystem::path directPath = std::filesystem::path(engineDir) / scriptName;
        if (detail::Exists(directPath)) {
            return directPath;
        }

        const std::filesystem::path nestedPath = std::filesystem::path("..") / engineDir / scriptName;
        if (detail::Exists(nestedPath)) {
            return nestedPath;
        }

        return directPath;
    }

}
