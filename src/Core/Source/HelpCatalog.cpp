#include "../Include/HelpCatalog.h"
#include "../Include/CatalogRegistry.h"

#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <unordered_set>

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
        if (const auto fallback = m_reachable.find("global"); fallback != m_reachable.end()) {
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
        const std::string normalized = ToLower(contextName);
        if (const auto it = m_commands.find(normalized); it != m_commands.end()) {
            return it->second;
        }
        return CommandsForGroup("global");
    }

    bool HelpCatalog::IsEngineGlobalCommand(std::string_view baseCommand) const {
        std::string normalized = ToLower(baseCommand);
        if (normalized.starts_with('/')) {
            normalized.erase(0, 1);
        }
        return m_engineGlobalBaseCommands.contains(normalized);
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
        m_engineGlobalBaseCommands.clear();
        m_lastError.clear();
        m_sourcePath = "<embedded:ui/pkg/catalog/data/commands_catalog.json>";

        const auto& registry = CatalogRegistry::Instance();
        if (!registry.IsLoaded()) {
            m_lastError = registry.LastError();
            return;
        }

        std::unordered_set<std::string> knownContexts;
        knownContexts.reserve(registry.Contexts().size());
        for (const auto& context : registry.Contexts()) {
            HelpContextEntry entry;
            entry.name = context.id;
            entry.description = context.description;
            m_contexts.push_back(std::move(entry));
            knownContexts.insert(context.id);
            m_reachable.emplace(context.id, context.reachable);
            m_commands.emplace(context.id, std::vector<HelpCommandEntry>{});
        }

        for (const auto& command : registry.Commands()) {
            HelpCommandEntry entry;
            entry.command = command.command;
            entry.synopsis = command.synopsis;

            if (command.scope.global) {
                for (auto& [contextId, bucket] : m_commands) {
                    (void)contextId;
                    bucket.push_back(entry);
                }
                if (std::ranges::find(command.owners, std::string("engine")) != command.owners.end()) {
                    std::string baseCommand = command.baseCommand;
                    if (baseCommand.starts_with('/')) {
                        baseCommand.erase(0, 1);
                    }
                    m_engineGlobalBaseCommands.insert(baseCommand);
                }
                continue;
            }

            for (const auto& contextId : command.scope.contexts) {
                if (!knownContexts.contains(contextId)) {
                    continue;
                }
                m_commands[contextId].push_back(entry);
            }
        }

        m_loaded = true;
    }

    std::string HelpCatalog::ToLower(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return result;
    }

    std::string HelpCatalog::BaseSlashCommand(std::string_view command) {
        const std::string lowered = ToLower(command);
        if (!lowered.starts_with('/')) {
            return lowered;
        }
        const auto split = lowered.find(' ');
        if (split == std::string::npos) {
            return lowered;
        }
        return lowered.substr(0, split);
    }

}

/*
HelpCatalog.cpp
Loads and exposes help/context metadata from the embedded commands catalog.
The catalog is shared by engine and TUI and no longer depends on runtime files.
*/
