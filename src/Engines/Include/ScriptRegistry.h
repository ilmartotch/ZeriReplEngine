#pragma once

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <nlohmann/json.hpp>
#include "../../Core/Include/RuntimeState.h"
#include "../../Core/Include/StringUtils.h"

namespace Zeri::Engines {

    struct ScriptVersionInfo {
        int version{ 0 };
        std::string timestamp;
        std::size_t sizeBytes{ 0 };
        bool current{ false };
    };

    struct ScriptDiffLine {
        enum class Kind {
            Neutral,
            Addition,
            Removal
        };
        Kind kind{ Kind::Neutral };
        std::string text;
    };

    struct ScriptSearchResult {
        std::string name;
        std::string language;
        std::string content;
        std::string modifiedUtc;
        std::size_t sizeBytes{ 0 };
        int rank{ 0 };
    };

    struct ScriptEntry {
        std::string name;
        std::string language;
        std::string modifiedUtc;
        std::size_t sizeBytes{ 0 };
        std::string content;
    };

    namespace Detail {
        struct ScriptMetaEntry {
            int version{ 0 };
            std::string timestampUtc;
            std::size_t sizeBytes{ 0 };
        };

        struct ScriptMeta {
            int version{ 0 };
            std::vector<ScriptMetaEntry> versions;
        };

        inline constexpr std::array<std::string_view, 5> kSupportedScriptLanguages = {
            "python", "lua", "js", "ts", "ruby"
        };

        [[nodiscard]] inline std::string BuildScriptKey(std::string_view lang, std::string_view name) {
            std::string key;
            key.reserve(lang.size() + name.size() + 11);
            key.append(lang);
            key.append("::scripts::");
            key.append(name);
            return key;
        }

        [[nodiscard]] inline std::string BuildVersionKey(std::string_view lang, std::string_view name, int version) {
            std::string key = BuildScriptKey(lang, name);
            key.append("::v::");
            key.append(std::to_string(version));
            return key;
        }

        [[nodiscard]] inline std::string BuildMetaKey(std::string_view lang, std::string_view name) {
            std::string key = BuildScriptKey(lang, name);
            key.append("::meta");
            return key;
        }

        [[nodiscard]] inline std::string BuildRegistryKey(std::string_view lang) {
            std::string key;
            key.reserve(lang.size() + 24);
            key.append(lang);
            key.append("::scripts::__registry__");
            return key;
        }

        [[nodiscard]] inline std::string BuildDeletedKey(std::string_view lang, std::string_view name) {
            std::string key;
            key.reserve(lang.size() + name.size() + 23);
            key.append(lang);
            key.append("::scripts::__deleted__::");
            key.append(name);
            return key;
        }

        [[nodiscard]] inline std::optional<std::string> AnyToString(const std::any& value) {
            if (!value.has_value()) {
                return std::nullopt;
            }
            if (value.type() == typeid(std::string)) {
                return std::any_cast<std::string>(value);
            }
            return std::nullopt;
        }

        [[nodiscard]] inline bool AnyToBool(const std::any& value) {
            if (!value.has_value()) {
                return false;
            }
            if (value.type() == typeid(bool)) {
                return std::any_cast<bool>(value);
            }
            return false;
        }

        [[nodiscard]] inline std::string UtcIso8601Now() {
            const auto now = std::chrono::system_clock::now();
            const std::time_t tt = std::chrono::system_clock::to_time_t(now);
            std::tm utcTm{};
#if defined(_WIN32)
            gmtime_s(&utcTm, &tt);
#else
            gmtime_r(&tt, &utcTm);
#endif
            std::ostringstream stream;
            stream << std::put_time(&utcTm, "%Y-%m-%dT%H:%M:%SZ");
            return stream.str();
        }

        [[nodiscard]] inline std::vector<std::string> ParseRegistry(const std::string& raw) {
            std::vector<std::string> names;
            std::istringstream stream(raw);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty()) {
                    names.push_back(line);
                }
            }
            return names;
        }

        [[nodiscard]] inline std::string SerializeRegistry(const std::vector<std::string>& names) {
            std::string raw;
            for (size_t i = 0; i < names.size(); ++i) {
                if (i > 0) {
                    raw.push_back('\n');
                }
                raw.append(names[i]);
            }
            return raw;
        }

        inline std::vector<std::string> ReadRegistry(
            Zeri::Core::RuntimeState& state,
            std::string_view lang
        ) {
            const auto registryAny = state.GetPersistedVariable(BuildRegistryKey(lang));
            const auto registry = AnyToString(registryAny);
            if (!registry.has_value()) {
                return {};
            }
            return ParseRegistry(*registry);
        }

        inline void WriteRegistry(
            Zeri::Core::RuntimeState& state,
            std::string_view lang,
            const std::vector<std::string>& names
        ) {
            state.SetPersistedVariable(BuildRegistryKey(lang), SerializeRegistry(names));
        }

        [[nodiscard]] inline ScriptMeta ParseMeta(const std::string& raw) {
            ScriptMeta parsed{};
            try {
                const auto root = nlohmann::json::parse(raw);
                parsed.version = root.value("version", 0);
                const auto versions = root.find("versions");
                if (versions == root.end() || !versions->is_array()) {
                    return parsed;
                }
                for (const auto& item : *versions) {
                    if (!item.is_object()) {
                        continue;
                    }
                    ScriptMetaEntry entry{};
                    entry.version = item.value("v", 0);
                    entry.timestampUtc = item.value("ts", "");
                    entry.sizeBytes = item.value("size", 0ULL);
                    if (entry.version > 0) {
                        parsed.versions.push_back(entry);
                    }
                }
            } catch (const std::exception&) {
                return {};
            }
            return parsed;
        }

        [[nodiscard]] inline ScriptMeta ReadMeta(
            Zeri::Core::RuntimeState& state,
            std::string_view lang,
            std::string_view name
        ) {
            const auto rawMeta = AnyToString(state.GetPersistedVariable(BuildMetaKey(lang, name)));
            if (!rawMeta.has_value()) {
                return {};
            }
            return ParseMeta(*rawMeta);
        }

        [[nodiscard]] inline std::string SerializeMeta(const ScriptMeta& meta) {
            nlohmann::json root = nlohmann::json::object();
            root["version"] = meta.version;
            root["versions"] = nlohmann::json::array();
            for (const auto& item : meta.versions) {
                root["versions"].push_back({
                    {"v", item.version},
                    {"ts", item.timestampUtc},
                    {"size", item.sizeBytes}
                });
            }
            return root.dump();
        }

        inline void WriteMeta(
            Zeri::Core::RuntimeState& state,
            std::string_view lang,
            std::string_view name,
            const ScriptMeta& meta
        ) {
            state.SetPersistedVariable(BuildMetaKey(lang, name), SerializeMeta(meta));
        }

        [[nodiscard]] inline std::vector<std::string> SplitLines(const std::string& input) {
            std::vector<std::string> lines;
            std::istringstream stream(input);
            std::string line;
            while (std::getline(stream, line)) {
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                lines.push_back(line);
            }
            if (!input.empty() && input.back() == '\n') {
                lines.emplace_back();
            }
            return lines;
        }
    }

    inline void SaveScript(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name,
        const std::string& code
    ) {
        auto meta = Detail::ReadMeta(state, lang, name);
        const int nextVersion = meta.version + 1;
        meta.version = nextVersion;
        meta.versions.push_back(Detail::ScriptMetaEntry{
            nextVersion,
            Detail::UtcIso8601Now(),
            code.size()
        });

        state.SetPersistedVariable(Detail::BuildVersionKey(lang, name, nextVersion), code);
        state.SetPersistedVariable(Detail::BuildScriptKey(lang, name), code);
        state.SetPersistedVariable(Detail::BuildDeletedKey(lang, name), false);
        Detail::WriteMeta(state, lang, name, meta);

        auto names = Detail::ReadRegistry(state, lang);
        if (std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
            Detail::WriteRegistry(state, lang, names);
        }
    }

    [[nodiscard]] inline std::optional<std::string> LoadScript(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name
    ) {
        const auto deletedAny = state.GetPersistedVariable(Detail::BuildDeletedKey(lang, name));
        if (Detail::AnyToBool(deletedAny)) {
            return std::nullopt;
        }

        const auto value = state.GetPersistedVariable(Detail::BuildScriptKey(lang, name));
        return Detail::AnyToString(value);
    }

    [[nodiscard]] inline bool DeleteScript(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name
    ) {
        const auto existing = LoadScript(state, lang, name);
        if (!existing.has_value()) {
            return false;
        }
        state.SetPersistedVariable(Detail::BuildDeletedKey(lang, name), true);
        return true;
    }

    [[nodiscard]] inline bool HardDeleteScript(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name
    ) {
        const auto names = Detail::ReadRegistry(state, lang);
        const bool inRegistry = std::find(names.begin(), names.end(), name) != names.end();
        const bool hasMeta = state.HasPersistedVariable(Detail::BuildMetaKey(lang, name));
        const bool hasLatest = state.HasPersistedVariable(Detail::BuildScriptKey(lang, name));
        if (!inRegistry && !hasMeta && !hasLatest) {
            return false;
        }

        auto mutableNames = names;
        mutableNames.erase(std::remove(mutableNames.begin(), mutableNames.end(), name), mutableNames.end());
        Detail::WriteRegistry(state, lang, mutableNames);

        const auto meta = Detail::ReadMeta(state, lang, name);
        for (const auto& item : meta.versions) {
            state.SetPersistedVariable(Detail::BuildVersionKey(lang, name, item.version), std::string{});
        }
        state.SetPersistedVariable(Detail::BuildMetaKey(lang, name), std::string{});
        state.SetPersistedVariable(Detail::BuildScriptKey(lang, name), std::string{});
        state.SetPersistedVariable(Detail::BuildDeletedKey(lang, name), true);
        return true;
    }

    [[nodiscard]] inline std::optional<std::string> LoadScriptVersion(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name,
        int version
    ) {
        if (version <= 0) {
            return std::nullopt;
        }

        const auto meta = Detail::ReadMeta(state, lang, name);
        const bool exists = std::any_of(
            meta.versions.begin(),
            meta.versions.end(),
            [version](const Detail::ScriptMetaEntry& item) {
                return item.version == version;
            }
        );
        if (!exists) {
            return std::nullopt;
        }

        return Detail::AnyToString(state.GetPersistedVariable(Detail::BuildVersionKey(lang, name, version)));
    }

    [[nodiscard]] inline std::vector<ScriptVersionInfo> GetScriptHistory(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name
    ) {
        const auto meta = Detail::ReadMeta(state, lang, name);
        std::vector<ScriptVersionInfo> history;
        history.reserve(meta.versions.size());
        for (const auto& item : meta.versions) {
            history.push_back(ScriptVersionInfo{
                item.version,
                item.timestampUtc,
                item.sizeBytes,
                item.version == meta.version
            });
        }
        return history;
    }

    [[nodiscard]] inline bool RollbackScript(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name,
        int version
    ) {
        const auto source = LoadScriptVersion(state, lang, name, version);
        if (!source.has_value()) {
            return false;
        }
        SaveScript(state, lang, name, *source);
        state.SetPersistedVariable(Detail::BuildDeletedKey(lang, name), false);
        return true;
    }

    [[nodiscard]] inline std::vector<ScriptDiffLine> BuildUnifiedDiff(
        const std::string& scriptName,
        int fromVersion,
        int toVersion,
        const std::string& fromContent,
        const std::string& toContent
    ) {
        const auto fromLines = Detail::SplitLines(fromContent);
        const auto toLines = Detail::SplitLines(toContent);
        if (fromLines == toLines) {
            return {};
        }

        const std::size_t n = fromLines.size();
        const std::size_t m = toLines.size();
        std::vector<std::vector<int>> lcs(n + 1, std::vector<int>(m + 1, 0));
        for (std::size_t i = n; i > 0; --i) {
            for (std::size_t j = m; j > 0; --j) {
                if (fromLines[i - 1] == toLines[j - 1]) {
                    lcs[i - 1][j - 1] = lcs[i][j] + 1;
                } else {
                    lcs[i - 1][j - 1] = std::max(lcs[i][j - 1], lcs[i - 1][j]);
                }
            }
        }

        std::vector<ScriptDiffLine> lines;
        lines.push_back({ ScriptDiffLine::Kind::Neutral, "--- " + scriptName + " v" + std::to_string(fromVersion) });
        lines.push_back({ ScriptDiffLine::Kind::Neutral, "+++ " + scriptName + " v" + std::to_string(toVersion) });
        lines.push_back({
            ScriptDiffLine::Kind::Neutral,
            "@@ -" + std::to_string(1) + "," + std::to_string(fromLines.size()) + " +" + std::to_string(1) + "," + std::to_string(toLines.size()) + " @@"
        });

        std::size_t i = 0;
        std::size_t j = 0;
        while (i < n || j < m) {
            if (i < n && j < m && fromLines[i] == toLines[j]) {
                lines.push_back({ ScriptDiffLine::Kind::Neutral, " " + fromLines[i] });
                ++i;
                ++j;
            } else if (j < m && (i == n || lcs[i][j + 1] >= lcs[i + 1][j])) {
                lines.push_back({ ScriptDiffLine::Kind::Addition, "+" + toLines[j] });
                ++j;
            } else if (i < n) {
                lines.push_back({ ScriptDiffLine::Kind::Removal, "-" + fromLines[i] });
                ++i;
            }
        }
        return lines;
    }

    [[nodiscard]] inline std::vector<std::string> ListScripts(
        Zeri::Core::RuntimeState& state,
        const std::string& lang
    ) {
        const auto names = Detail::ReadRegistry(state, lang);
        std::vector<std::string> active;
        active.reserve(names.size());
        for (const auto& name : names) {
            if (Detail::AnyToBool(state.GetPersistedVariable(Detail::BuildDeletedKey(lang, name)))) {
                continue;
            }
            active.push_back(name);
        }
        return active;
    }

    [[nodiscard]] inline std::vector<ScriptSearchResult> SearchScripts(
        Zeri::Core::RuntimeState& state,
        const std::string& query
    ) {
        const std::string lowered = Zeri::Core::Utils::ToLower(query);
        std::vector<ScriptSearchResult> results;
        for (const auto language : Detail::kSupportedScriptLanguages) {
            const auto names = ListScripts(state, std::string(language));
            for (const auto& name : names) {
                const auto content = LoadScript(state, std::string(language), name);
                if (!content.has_value()) {
                    continue;
                }

                const std::string nameLower = Zeri::Core::Utils::ToLower(name);
                const std::string langLower = Zeri::Core::Utils::ToLower(language);
                const std::string contentLower = Zeri::Core::Utils::ToLower(*content);
                const bool nameMatch = lowered.empty() || nameLower.find(lowered) != std::string::npos;
                const bool langMatch = !nameMatch && (langLower.find(lowered) != std::string::npos);
                const bool contentMatch = !nameMatch && !langMatch && (contentLower.find(lowered) != std::string::npos);
                if (!nameMatch && !langMatch && !contentMatch) {
                    continue;
                }

                const auto history = GetScriptHistory(state, std::string(language), name);
                std::string modifiedUtc;
                if (!history.empty()) {
                    const auto current = std::find_if(history.begin(), history.end(), [](const ScriptVersionInfo& item) {
                        return item.current;
                    });
                    if (current != history.end()) {
                        modifiedUtc = current->timestamp;
                    } else {
                        modifiedUtc = history.back().timestamp;
                    }
                }

                int rank = 100;
                if (nameMatch) {
                    rank = 300;
                } else if (langMatch) {
                    rank = 200;
                }

                results.push_back(ScriptSearchResult{
                    name,
                    std::string(language),
                    *content,
                    modifiedUtc,
                    content->size(),
                    rank
                });
            }
        }

        std::sort(results.begin(), results.end(), [](const ScriptSearchResult& lhs, const ScriptSearchResult& rhs) {
            if (lhs.rank != rhs.rank) {
                return lhs.rank > rhs.rank;
            }
            if (lhs.modifiedUtc != rhs.modifiedUtc) {
                return lhs.modifiedUtc > rhs.modifiedUtc;
            }
            if (lhs.name != rhs.name) {
                return lhs.name < rhs.name;
            }
            return lhs.language < rhs.language;
        });
        return results;
    }

    [[nodiscard]] inline std::vector<ScriptEntry> ListScriptsWithContent(Zeri::Core::RuntimeState& state) {
        std::vector<ScriptEntry> scripts;
        for (const auto language : Detail::kSupportedScriptLanguages) {
            const auto names = ListScripts(state, std::string(language));
            for (const auto& name : names) {
                const auto content = LoadScript(state, std::string(language), name);
                if (!content.has_value()) {
                    continue;
                }
                std::string modifiedUtc;
                const auto history = GetScriptHistory(state, std::string(language), name);
                if (!history.empty()) {
                    const auto current = std::find_if(history.begin(), history.end(), [](const ScriptVersionInfo& item) {
                        return item.current;
                    });
                    if (current != history.end()) {
                        modifiedUtc = current->timestamp;
                    } else {
                        modifiedUtc = history.back().timestamp;
                    }
                }

                scripts.push_back(ScriptEntry{
                    name,
                    std::string(language),
                    modifiedUtc,
                    content->size(),
                    *content
                });
            }
        }

        std::sort(scripts.begin(), scripts.end(), [](const ScriptEntry& lhs, const ScriptEntry& rhs) {
            if (lhs.modifiedUtc != rhs.modifiedUtc) {
                return lhs.modifiedUtc > rhs.modifiedUtc;
            }
            if (lhs.language != rhs.language) {
                return lhs.language < rhs.language;
            }
            return lhs.name < rhs.name;
        });
        return scripts;
    }

}

/*
ScriptRegistry.h
Header-only helper for CRUD operations on saved scripts using RuntimeState::Persisted scope.
Script keys follow the required format "{lang}::scripts::{name}" and now include versioning:
latest key, immutable snapshots "::v::<N>", and metadata "::meta" with ISO8601 UTC timestamps.
The implementation remains backward compatible for existing latest keys and still avoids direct
persisted-scope erase by relying on registry/deletion markers for visibility semantics.
*/
