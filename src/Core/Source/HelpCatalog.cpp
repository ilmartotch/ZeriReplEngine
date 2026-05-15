#include "../Include/HelpCatalog.h"
#include "../Include/AppPaths.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <stdexcept>
#include <sstream>
#include <vector>

namespace Zeri::Core {

    namespace {
        [[nodiscard]] std::string ReadEnvVar(const char* name) {
#if defined(_WIN32)
            char* value = nullptr;
            std::size_t length = 0;
            if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
                return {};
            }
            std::string out(value);
            std::free(value);
            return out;
#else
            if (const char* value = std::getenv(name); value != nullptr) {
                return std::string(value);
            }
            return {};
#endif
        }

        [[nodiscard]] std::vector<unsigned char> ReadAllBytes(
            const std::filesystem::path& path,
            std::string& error
        ) {
            std::ifstream stream(path, std::ios::binary);
            if (!stream.is_open()) {
                error = "Failed to open help catalog file: " + path.string();
                return {};
            }

            stream.seekg(0, std::ios::end);
            const std::streamoff size = stream.tellg();
            if (size < 0) {
                error = "Failed to read help catalog size: " + path.string();
                return {};
            }

            stream.seekg(0, std::ios::beg);
            std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
            if (!bytes.empty()) {
                stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
                if (!stream.good() && !stream.eof()) {
                    error = "Failed while reading help catalog bytes: " + path.string();
                    return {};
                }
            }

            return bytes;
        }

        [[nodiscard]] std::string DecodeUtf8JsonText(
            const std::vector<unsigned char>& bytes,
            const std::filesystem::path& path,
            std::string& error
        ) {
            if (bytes.empty()) {
                error = "Help catalog file is empty: " + path.string();
                return {};
            }

            if (bytes.size() >= 2U) {
                const bool utf16LeBom = bytes[0] == 0xFFU && bytes[1] == 0xFEU;
                const bool utf16BeBom = bytes[0] == 0xFEU && bytes[1] == 0xFFU;
                if (utf16LeBom || utf16BeBom) {
                    error = "Help catalog uses UTF-16 encoding. Use UTF-8 JSON: " + path.string();
                    return {};
                }
            }

            const bool hasNullByte = std::find(bytes.begin(), bytes.end(), static_cast<unsigned char>(0)) != bytes.end();
            if (hasNullByte) {
                error = "Help catalog contains NUL bytes and is not valid UTF-8 text JSON: " + path.string();
                return {};
            }

            std::size_t offset = 0U;
            if (bytes.size() >= 3U && bytes[0] == 0xEFU && bytes[1] == 0xBBU && bytes[2] == 0xBFU) {
                offset = 3U;
            }

            return std::string(
                reinterpret_cast<const char*>(bytes.data() + offset),
                reinterpret_cast<const char*>(bytes.data() + bytes.size())
            );
        }
    }

    HelpCatalog& HelpCatalog::Instance() {
        static HelpCatalog instance;
        return instance;
    }

    HelpCatalog::HelpCatalog() {
        Load();
    }

    const std::vector<HelpContextEntry>& HelpCatalog::Contexts() const {
        return m_contexts;
    }

    const HelpContextEntry* HelpCatalog::FindContext(std::string_view name) const {
        const std::string normalized = ToLower(name);
        for (const auto& context : m_contexts) {
            if (ToLower(context.name) == normalized) {
                return &context;
            }
        }
        return nullptr;
    }

    std::vector<std::string> HelpCatalog::ReachableFrom(std::string_view contextName) const {
        const std::string normalized = ToLower(contextName);

        if (const auto it = m_reachable.find(normalized); it != m_reachable.end()) {
            return it->second;
        }

        if (const auto fallback = m_reachable.find("default"); fallback != m_reachable.end()) {
            return fallback->second;
        }

        return { "global" };
    }

    const std::vector<HelpCommandEntry>& HelpCatalog::CommandsForGroup(std::string_view group) const {
        static const std::vector<HelpCommandEntry> kEmpty;

        const std::string normalized = ToLower(group);
        if (const auto it = m_commands.find(normalized); it != m_commands.end()) {
            return it->second;
        }

        return kEmpty;
    }

    std::vector<HelpCommandEntry> HelpCatalog::CommandsForContext(std::string_view contextName) const {
        std::vector<HelpCommandEntry> result;

        const auto& global = CommandsForGroup("global");
        result.insert(result.end(), global.begin(), global.end());

        const std::string normalized = ToLower(contextName);
        if (normalized == "js" || normalized == "ts" || normalized == "lua" || normalized == "python" || normalized == "ruby") {
            const auto& script = CommandsForGroup("script");
            result.insert(result.end(), script.begin(), script.end());
            return result;
        }

        const auto& specific = CommandsForGroup(normalized);
        result.insert(result.end(), specific.begin(), specific.end());
        return result;
    }

    bool HelpCatalog::IsLoaded() const {
        return m_loaded;
    }

    const std::string& HelpCatalog::LastError() const {
        return m_lastError;
    }

    const std::filesystem::path& HelpCatalog::SourcePath() const {
        return m_sourcePath;
    }

    void HelpCatalog::Load() {
        m_loaded = false;
        m_contexts.clear();
        m_reachable.clear();
        m_commands.clear();
        m_lastError.clear();

        try {
            const auto path = ResolveCatalogPath();
            m_sourcePath = path;

            if (!std::filesystem::exists(path)) {
                m_lastError = "Help catalog file not found: " + path.string();
                return;
            }

            std::string readError;
            const auto bytes = ReadAllBytes(path, readError);
            if (!readError.empty()) {
                m_lastError = readError;
                return;
            }

            std::string decodeError;
            const std::string jsonText = DecodeUtf8JsonText(bytes, path, decodeError);
            if (!decodeError.empty()) {
                m_lastError = decodeError;
                return;
            }

            nlohmann::json doc = nlohmann::json::parse(jsonText);

            for (const auto& context : doc.at("contexts")) {
                HelpContextEntry entry;
                entry.name = context.at("name").get<std::string>();
                entry.description = context.at("description").get<std::string>();
                m_contexts.push_back(std::move(entry));
            }

            for (const auto& [key, values] : doc.at("reachable").items()) {
                std::vector<std::string> targets;
                for (const auto& value : values) {
                    targets.push_back(ToLower(value.get<std::string>()));
                }
                m_reachable.emplace(ToLower(key), std::move(targets));
            }

            for (const auto& [group, values] : doc.at("commands").items()) {
                std::vector<HelpCommandEntry> entries;
                for (const auto& item : values) {
                    HelpCommandEntry command;
                    command.command = item.at("command").get<std::string>();
                    command.synopsis = item.at("synopsis").get<std::string>();
                    entries.push_back(std::move(command));
                }
                m_commands.emplace(ToLower(group), std::move(entries));
            }

            m_loaded = true;
        } catch (const nlohmann::json::parse_error& e) {
            std::ostringstream oss;
            oss << "Failed to parse help catalog JSON at byte " << e.byte << ": " << e.what();
            m_lastError = oss.str();
            m_loaded = false;
        } catch (const nlohmann::json::type_error& e) {
            m_lastError = std::string("Help catalog schema type error: ") + e.what();
            m_loaded = false;
        } catch (const nlohmann::json::out_of_range& e) {
            m_lastError = std::string("Help catalog schema key is missing: ") + e.what();
            m_loaded = false;
        } catch (const std::exception& e) {
            m_lastError = std::string("Failed to load help catalog: ") + e.what();
            m_loaded = false;
        }
    }

    std::filesystem::path HelpCatalog::ResolveCatalogPath() {
        if (const std::string explicitPath = ReadEnvVar("ZERI_HELP_CATALOG_PATH"); !explicitPath.empty()) {
            return std::filesystem::path(explicitPath);
        }

        const std::filesystem::path exeDir = ResolveExecutableDir();
        const auto exeCandidate = exeDir / "help" / "help_catalog.json";
        if (std::filesystem::exists(exeCandidate)) {
            return exeCandidate;
        }

        std::filesystem::path current = std::filesystem::current_path();
        while (!current.empty()) {
            const auto candidate = current / "help" / "help_catalog.json";
            if (std::filesystem::exists(candidate)) {
                return candidate;
            }

            const auto parent = current.parent_path();
            if (parent == current) {
                break;
            }
            current = parent;
        }

        return exeCandidate;
    }

    std::string HelpCatalog::ToLower(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return result;
    }

}

/*
HelpCatalog.cpp
Loads and exposes shared help metadata from `help/help_catalog.json`.
The catalog is consumed by engine and terminal UI to keep command/context text synchronized.
Load validates UTF-8 text input, rejects UTF-16/NUL-byte payloads, and records actionable
failure diagnostics for CI and runtime reporting.
*/
