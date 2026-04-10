#include "../Include/HelpCatalog.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace Zeri::Core {

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

    void HelpCatalog::Load() {
        const auto path = ResolveCatalogPath();
        std::ifstream stream(path);
        if (!stream.is_open()) {
            throw std::runtime_error("Unable to open help catalog: " + path.string());
        }

        nlohmann::json doc;
        stream >> doc;

        m_contexts.clear();
        m_reachable.clear();
        m_commands.clear();

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
    }

    std::filesystem::path HelpCatalog::ResolveCatalogPath() {
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

        return std::filesystem::path("help") / "help_catalog.json";
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
*/
