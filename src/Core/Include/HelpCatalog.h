#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <filesystem>

namespace Zeri::Core {

    struct HelpContextEntry {
        std::string name;
        std::string description;
    };

    struct HelpCommandEntry {
        std::string command;
        std::string synopsis;
    };

    class HelpCatalog {
    public:
        static HelpCatalog& Instance();

        [[nodiscard]] const std::vector<HelpContextEntry>& Contexts() const;
        [[nodiscard]] const HelpContextEntry* FindContext(std::string_view name) const;
        [[nodiscard]] std::vector<std::string> ReachableFrom(std::string_view contextName) const;
        [[nodiscard]] const std::vector<HelpCommandEntry>& CommandsForGroup(std::string_view group) const;
        [[nodiscard]] std::vector<HelpCommandEntry> CommandsForContext(std::string_view contextName) const;

    private:
        HelpCatalog();

        void Load();
        [[nodiscard]] static std::filesystem::path ResolveCatalogPath();
        [[nodiscard]] static std::string ToLower(std::string_view value);

        std::vector<HelpContextEntry> m_contexts;
        std::unordered_map<std::string, std::vector<std::string>> m_reachable;
        std::unordered_map<std::string, std::vector<HelpCommandEntry>> m_commands;
    };

}

/*
HelpCatalog.h
Defines the shared help/command metadata API used by engine and terminal UI.
Data is loaded from `help/help_catalog.json` and exposed through a process-wide singleton.
*/
