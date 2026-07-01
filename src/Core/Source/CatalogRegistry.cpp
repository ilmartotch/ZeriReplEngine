#include "../Include/CatalogRegistry.h"
#include "../Include/StringUtils.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ranges>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>

#include "embedded_catalog_assets.h"

namespace Zeri::Core {

    namespace {
        constexpr int kSupportedCatalogVersion = 1;

        [[nodiscard]] int ReadVersion(const nlohmann::json& document, std::string_view catalogName) {
            if (!document.contains("version") || !document["version"].is_number_integer()) {
                throw std::runtime_error(std::string(catalogName) + " catalog is missing integer version.");
            }
            const int version = document["version"].get<int>();
            if (version <= 0) {
                throw std::runtime_error(std::string(catalogName) + " catalog version must be > 0.");
            }
            if (version > kSupportedCatalogVersion) {
                throw std::runtime_error(
                    std::string(catalogName) + " catalog version " + std::to_string(version) +
                    " is not supported (max " + std::to_string(kSupportedCatalogVersion) + ")."
                );
            }
            return version;
        }

        [[nodiscard]] std::vector<std::string> JsonStringArray(const nlohmann::json& node) {
            std::vector<std::string> result;
            if (!node.is_array()) {
                return result;
            }
            for (const auto& item : node) {
                if (!item.is_string()) {
                    continue;
                }
                result.push_back(item.get<std::string>());
            }
            return result;
        }

    }

    CatalogRegistry& CatalogRegistry::Instance() {
        static CatalogRegistry instance;
        return instance;
    }

    CatalogRegistry::CatalogRegistry() {
        Load();
    }

    bool CatalogRegistry::IsLoaded() const {
        return m_loaded;
    }

    const std::string& CatalogRegistry::LastError() const {
        return m_lastError;
    }

    const std::vector<CatalogContextRecord>& CatalogRegistry::Contexts() const {
        return m_contexts;
    }

    const std::vector<CatalogCommandRecord>& CatalogRegistry::Commands() const {
        return m_commands;
    }

    const std::vector<CatalogErrorRecord>& CatalogRegistry::Errors() const {
        return m_errors;
    }

    const std::vector<CatalogLanguageRecord>& CatalogRegistry::Languages() const {
        return m_languages;
    }

    const std::vector<CatalogBridgeTypeRecord>& CatalogRegistry::BridgeTypes() const {
        return m_bridgeTypes;
    }

    const CatalogContextRecord* CatalogRegistry::FindContext(std::string_view id) const {
        const auto it = m_contextIndexById.find(Utils::ToLower(id));
        if (it == m_contextIndexById.end()) {
            return nullptr;
        }
        return &m_contexts[it->second];
    }

    const CatalogErrorRecord* CatalogRegistry::FindError(std::string_view code) const {
        const auto it = m_errorIndexByCode.find(ToUpper(code));
        if (it == m_errorIndexByCode.end()) {
            return nullptr;
        }
        return &m_errors[it->second];
    }

    const CatalogLanguageRecord* CatalogRegistry::ResolveLanguage(std::string_view idOrAlias) const {
        const auto it = m_languageIndexByAlias.find(Utils::ToLower(idOrAlias));
        if (it == m_languageIndexByAlias.end()) {
            return nullptr;
        }
        return &m_languages[it->second];
    }

    const CatalogBridgeTypeRecord* CatalogRegistry::FindBridgeType(std::string_view id) const {
        const auto it = m_bridgeTypeIndexById.find(ToUpper(id));
        if (it == m_bridgeTypeIndexById.end()) {
            return nullptr;
        }
        return &m_bridgeTypes[it->second];
    }

    bool CatalogRegistry::IsEngineGlobalCommandBase(std::string_view baseCommand) const {
        const std::string normalized = Utils::ToLower(baseCommand);
        for (const auto& command : m_commands) {
            if (command.baseCommand != normalized) {
                continue;
            }
            if (!command.scope.global) {
                continue;
            }
            if (HasOwner(command, "engine")) {
                return true;
            }
        }
        return false;
    }

    std::vector<std::string> CatalogRegistry::AllowedContextsForBase(
        std::string_view baseCommand,
        std::string_view owner
    ) const {
        const std::string normalizedBase = Utils::ToLower(baseCommand);
        const std::string normalizedOwner = Utils::ToLower(owner);
        std::set<std::string> contexts;
        bool hasGlobal = false;

        for (const auto& command : m_commands) {
            if (command.baseCommand != normalizedBase) {
                continue;
            }
            if (!normalizedOwner.empty() && !HasOwner(command, normalizedOwner)) {
                continue;
            }
            if (command.scope.global) {
                hasGlobal = true;
                break;
            }
            for (const auto& context : command.scope.contexts) {
                contexts.insert(context);
            }
        }

        if (hasGlobal) {
            return { "global" };
        }
        return { contexts.begin(), contexts.end() };
    }

    void CatalogRegistry::Load() {
        m_loaded = false;
        m_lastError.clear();
        m_contexts.clear();
        m_commands.clear();
        m_errors.clear();
        m_languages.clear();
        m_bridgeTypes.clear();
        m_contextIndexById.clear();
        m_errorIndexByCode.clear();
        m_languageIndexByAlias.clear();
        m_bridgeTypeIndexById.clear();

        try {
            const auto commandsJson = nlohmann::json::parse(kEmbeddedCommandsCatalogJson);
            const auto errorsJson = nlohmann::json::parse(kEmbeddedErrorsCatalogJson);
            const auto languagesJson = nlohmann::json::parse(kEmbeddedLanguagesCatalogJson);
            const auto bridgeTypesJson = nlohmann::json::parse(kEmbeddedBridgeTypesCatalogJson);

            const int commandsVersion = ReadVersion(commandsJson, "commands");
            const int errorsVersion = ReadVersion(errorsJson, "errors");
            const int languagesVersion = ReadVersion(languagesJson, "languages");
            const int bridgeTypesVersion = ReadVersion(bridgeTypesJson, "bridge types");
            (void)commandsVersion;
            (void)errorsVersion;
            (void)languagesVersion;
            (void)bridgeTypesVersion;

            for (const auto& contextNode : commandsJson.at("contexts")) {
                CatalogContextRecord context;
                context.id = Utils::ToLower(Utils::Trim(contextNode.at("id").get<std::string>()));
                context.description = Utils::Trim(contextNode.at("description").get<std::string>());
                context.reachable = JsonStringArray(contextNode.value("reachable", nlohmann::json::array()));
                if (context.id.empty()) {
                    throw std::runtime_error("commands catalog has an empty context id.");
                }
                if (context.description.empty()) {
                    throw std::runtime_error("commands catalog context '" + context.id + "' has an empty description.");
                }
                if (context.reachable.empty()) {
                    context.reachable.push_back(context.id);
                }
                for (auto& target : context.reachable) {
                    target = Utils::ToLower(Utils::Trim(target));
                }
                if (m_contextIndexById.contains(context.id)) {
                    throw std::runtime_error("commands catalog has duplicated context id '" + context.id + "'.");
                }
                m_contextIndexById.emplace(context.id, m_contexts.size());
                m_contexts.push_back(std::move(context));
            }
            if (!m_contextIndexById.contains("global")) {
                throw std::runtime_error("commands catalog must include the 'global' context.");
            }
            for (const auto& context : m_contexts) {
                for (const auto& target : context.reachable) {
                    if (!m_contextIndexById.contains(target)) {
                        throw std::runtime_error(
                            "commands catalog context '" + context.id + "' references unknown reachable context '" + target + "'."
                        );
                    }
                }
            }

            for (const auto& commandNode : commandsJson.at("commands")) {
                CatalogCommandRecord command;
                command.id = Utils::Trim(commandNode.at("id").get<std::string>());
                command.command = Utils::Trim(commandNode.at("command").get<std::string>());
                command.synopsis = Utils::Trim(commandNode.at("synopsis").get<std::string>());
                command.baseCommand = ExtractBaseCommand(command.command);
                if (command.id.empty() || command.command.empty() || command.synopsis.empty() || command.baseCommand.empty()) {
                    throw std::runtime_error("commands catalog entries require id, command, synopsis and valid base command.");
                }

                const auto scopeNode = commandNode.at("scope");
                const std::string scopeType = Utils::ToLower(Utils::Trim(scopeNode.at("type").get<std::string>()));
                if (scopeType == "global") {
                    command.scope.global = true;
                } else if (scopeType == "context") {
                    command.scope.global = false;
                    command.scope.contexts = JsonStringArray(scopeNode.value("contexts", nlohmann::json::array()));
                    if (command.scope.contexts.empty()) {
                        throw std::runtime_error("commands catalog command '" + command.id + "' has empty context scope.");
                    }
                    std::set<std::string> deduped;
                    for (auto& contextId : command.scope.contexts) {
                        contextId = Utils::ToLower(Utils::Trim(contextId));
                        if (!m_contextIndexById.contains(contextId)) {
                            throw std::runtime_error(
                                "commands catalog command '" + command.id + "' references unknown context '" + contextId + "'."
                            );
                        }
                        deduped.insert(contextId);
                    }
                    command.scope.contexts.assign(deduped.begin(), deduped.end());
                } else {
                    throw std::runtime_error("commands catalog command '" + command.id + "' has invalid scope type '" + scopeType + "'.");
                }

                command.owners = JsonStringArray(commandNode.value("owners", nlohmann::json::array()));
                if (command.owners.empty()) {
                    command.owners = { "engine", "tui" };
                }
                for (auto& owner : command.owners) {
                    owner = Utils::ToLower(Utils::Trim(owner));
                }

                m_commands.push_back(std::move(command));
            }

            for (const auto& entryNode : errorsJson.at("entries")) {
                CatalogErrorRecord entry;
                entry.code = ToUpper(Utils::Trim(entryNode.at("code").get<std::string>()));
                entry.message = Utils::Trim(entryNode.at("message").get<std::string>());
                entry.trigger = Utils::Trim(entryNode.at("trigger").get<std::string>());
                entry.hint = Utils::Trim(entryNode.at("hint").get<std::string>());
                if (entry.code.empty() || entry.message.empty() || entry.hint.empty()) {
                    throw std::runtime_error("errors catalog entries require code, message and hint.");
                }
                if (!m_errorIndexByCode.contains(entry.code)) {
                    m_errorIndexByCode.emplace(entry.code, m_errors.size());
                    m_errors.push_back(std::move(entry));
                }
            }

            for (const auto& languageNode : languagesJson.at("languages")) {
                CatalogLanguageRecord language;
                language.id = Utils::ToLower(Utils::Trim(languageNode.at("id").get<std::string>()));
                language.aliases = JsonStringArray(languageNode.value("aliases", nlohmann::json::array()));
                language.extension = Utils::ToLower(Utils::Trim(languageNode.at("extension").get<std::string>()));
                language.folder = Utils::ToLower(Utils::Trim(languageNode.at("folder").get<std::string>()));
                language.runtime = Utils::ToLower(Utils::Trim(languageNode.at("runtime").get<std::string>()));
                language.context = Utils::ToLower(Utils::Trim(languageNode.at("context").get<std::string>()));
                if (
                    language.id.empty() || language.extension.empty() || language.folder.empty() ||
                    language.runtime.empty() || language.context.empty()
                ) {
                    throw std::runtime_error("languages catalog entries require id, extension, folder, runtime and context.");
                }
                if (!m_contextIndexById.contains(language.context)) {
                    throw std::runtime_error(
                        "languages catalog language '" + language.id + "' references unknown context '" + language.context + "'."
                    );
                }

                const std::size_t index = m_languages.size();
                m_languages.push_back(language);

                auto registerAlias = [this, index](std::string aliasRaw) {
                    const std::string alias = Utils::ToLower(Utils::Trim(aliasRaw));
                    if (alias.empty()) {
                        return;
                    }
                    if (const auto it = m_languageIndexByAlias.find(alias); it != m_languageIndexByAlias.end()) {
                        if (it->second != index) {
                            throw std::runtime_error("languages catalog has duplicated alias '" + alias + "'.");
                        }
                        return;
                    }
                    m_languageIndexByAlias.emplace(alias, index);
                };

                registerAlias(language.id);
                registerAlias(language.folder);
                registerAlias(language.context);
                for (const auto& alias : language.aliases) {
                    registerAlias(alias);
                }
            }

            for (const auto& bridgeTypeNode : bridgeTypesJson.at("types")) {
                CatalogBridgeTypeRecord bridgeType;
                bridgeType.id = ToUpper(Utils::Trim(bridgeTypeNode.at("id").get<std::string>()));
                bridgeType.value = Utils::ToLower(Utils::Trim(bridgeTypeNode.at("value").get<std::string>()));
                if (bridgeType.id.empty() || bridgeType.value.empty()) {
                    throw std::runtime_error("bridge types catalog entries require id and value.");
                }
                if (m_bridgeTypeIndexById.contains(bridgeType.id)) {
                    throw std::runtime_error("bridge types catalog has duplicated id '" + bridgeType.id + "'.");
                }
                m_bridgeTypeIndexById.emplace(bridgeType.id, m_bridgeTypes.size());
                m_bridgeTypes.push_back(std::move(bridgeType));
            }

            m_loaded = true;
        } catch (const std::exception& e) {
            m_lastError = e.what();
            m_loaded = false;
        }
    }

    std::string CatalogRegistry::ToUpper(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            result.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        return result;
    }

    std::string CatalogRegistry::ExtractBaseCommand(std::string_view command) {
        const std::string lowered = Utils::ToLower(Utils::Trim(command));
        if (!lowered.starts_with('/')) {
            return lowered;
        }
        const auto firstSpace = lowered.find(' ');
        if (firstSpace == std::string::npos) {
            return lowered;
        }
        return lowered.substr(0, firstSpace);
    }

    bool CatalogRegistry::HasOwner(const CatalogCommandRecord& command, std::string_view owner) {
        const std::string normalized = Utils::ToLower(owner);
        return std::ranges::find(command.owners, normalized) != command.owners.end();
    }

}
