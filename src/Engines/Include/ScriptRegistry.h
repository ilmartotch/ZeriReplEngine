#pragma once

#include <algorithm>
#include <any>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include "../../Core/Include/RuntimeState.h"

namespace Zeri::Engines {

    namespace Detail {
        [[nodiscard]] inline std::string BuildScriptKey(std::string_view lang, std::string_view name) {
            std::string key;
            key.reserve(lang.size() + name.size() + 11);
            key.append(lang);
            key.append("::scripts::");
            key.append(name);
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
    }

    inline void SaveScript(
        Zeri::Core::RuntimeState& state,
        const std::string& lang,
        const std::string& name,
        const std::string& code
    ) {
        state.SetPersistedVariable(Detail::BuildScriptKey(lang, name), code);
        state.SetPersistedVariable(Detail::BuildDeletedKey(lang, name), false);

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
        auto names = Detail::ReadRegistry(state, lang);
        auto it = std::find(names.begin(), names.end(), name);
        if (it == names.end()) {
            return false;
        }

        names.erase(it);
        Detail::WriteRegistry(state, lang, names);
        state.SetPersistedVariable(Detail::BuildDeletedKey(lang, name), true);
        return true;
    }

    [[nodiscard]] inline std::vector<std::string> ListScripts(
        Zeri::Core::RuntimeState& state,
        const std::string& lang
    ) {
        return Detail::ReadRegistry(state, lang);
    }

}

/*
ScriptRegistry.h
Header-only helper per CRUD di script salvati usando RuntimeState::Persisted scope.
Le chiavi script seguono il formato richiesto "{lang}::scripts::{name}".
Dato che RuntimeState non espone erase/iterazione diretta del persisted scope, viene mantenuto
un registro per linguaggio ("{lang}::scripts::__registry__") e un marker di cancellazione,
cosi' DeleteScript/LoadScript/ListScripts restano coerenti e non dipendono da filesystem.
*/
