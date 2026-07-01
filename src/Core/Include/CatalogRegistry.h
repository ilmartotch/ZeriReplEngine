#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Zeri::Core {

    struct CatalogContextRecord {
        std::string id;
        std::string description;
        std::vector<std::string> reachable;
    };

    struct CatalogCommandScopeRecord {
        bool global{ false };
        std::vector<std::string> contexts;
    };

    struct CatalogCommandRecord {
        std::string id;
        std::string command;
        std::string synopsis;
        std::string baseCommand;
        CatalogCommandScopeRecord scope;
        std::vector<std::string> owners;
    };

    struct CatalogErrorRecord {
        std::string code;
        std::string message;
        std::string trigger;
        std::string hint;
    };

    struct CatalogLanguageRecord {
        std::string id;
        std::vector<std::string> aliases;
        std::string extension;
        std::string folder;
        std::string runtime;
        std::string context;
    };

    struct CatalogBridgeTypeRecord {
        std::string id;
        std::string value;
    };

    class CatalogRegistry {
    public:
        static CatalogRegistry& Instance();

        [[nodiscard]] bool IsLoaded() const;
        [[nodiscard]] const std::string& LastError() const;

        [[nodiscard]] const std::vector<CatalogContextRecord>& Contexts() const;
        [[nodiscard]] const std::vector<CatalogCommandRecord>& Commands() const;
        [[nodiscard]] const std::vector<CatalogErrorRecord>& Errors() const;
        [[nodiscard]] const std::vector<CatalogLanguageRecord>& Languages() const;
        [[nodiscard]] const std::vector<CatalogBridgeTypeRecord>& BridgeTypes() const;

        [[nodiscard]] const CatalogContextRecord* FindContext(std::string_view id) const;
        [[nodiscard]] const CatalogErrorRecord* FindError(std::string_view code) const;
        [[nodiscard]] const CatalogLanguageRecord* ResolveLanguage(std::string_view idOrAlias) const;
        [[nodiscard]] const CatalogBridgeTypeRecord* FindBridgeType(std::string_view id) const;

        [[nodiscard]] bool IsEngineGlobalCommandBase(std::string_view baseCommand) const;
        [[nodiscard]] std::vector<std::string> AllowedContextsForBase(
            std::string_view baseCommand,
            std::string_view owner
        ) const;

    private:
        CatalogRegistry();
        void Load();

        [[nodiscard]] static std::string ToUpper(std::string_view value);
        [[nodiscard]] static std::string ExtractBaseCommand(std::string_view command);
        [[nodiscard]] static bool HasOwner(const CatalogCommandRecord& command, std::string_view owner);

        bool m_loaded{ false };
        std::string m_lastError;

        std::vector<CatalogContextRecord> m_contexts;
        std::vector<CatalogCommandRecord> m_commands;
        std::vector<CatalogErrorRecord> m_errors;
        std::vector<CatalogLanguageRecord> m_languages;
        std::vector<CatalogBridgeTypeRecord> m_bridgeTypes;

        std::unordered_map<std::string, std::size_t> m_contextIndexById;
        std::unordered_map<std::string, std::size_t> m_errorIndexByCode;
        std::unordered_map<std::string, std::size_t> m_languageIndexByAlias;
        std::unordered_map<std::string, std::size_t> m_bridgeTypeIndexById;
    };

}
