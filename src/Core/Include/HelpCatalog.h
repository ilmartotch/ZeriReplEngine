#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>
#include <unordered_set>
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
        [[nodiscard]] bool IsEngineGlobalCommand(std::string_view baseCommand) const;
        [[nodiscard]] bool IsLoaded() const;
        [[nodiscard]] const std::string& LastError() const;
        [[nodiscard]] const std::filesystem::path& SourcePath() const;

    private:
        HelpCatalog();

        void Load();
        [[nodiscard]] static std::string ToLower(std::string_view value);
        [[nodiscard]] static std::string BaseSlashCommand(std::string_view command);

        std::vector<HelpContextEntry> m_contexts;
        std::unordered_map<std::string, std::vector<std::string>> m_reachable;
        std::unordered_map<std::string, std::vector<HelpCommandEntry>> m_commands;
        std::unordered_set<std::string> m_engineGlobalBaseCommands;
        bool m_loaded{ false };
        std::filesystem::path m_sourcePath;
        std::string m_lastError;
    };

}

/*
HelpCatalog.h
Defines the shared help/command metadata API used by engine and terminal UI.
Data is loaded from the embedded command catalog JSON and exposed through a process-wide singleton.
LastError() and SourcePath() expose failure diagnostics for runtime and smoke tests.
*/
