#include "../Include/VariableScope.h"
#include <algorithm>

namespace Zeri::Core {

    std::string VariableScope::NormalizeKey(const std::string& key) const {
        std::string normalized = key;
        std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
        return normalized;
    }

    void VariableScope::Set(const std::string& key, const std::any& value, ScopeLevel level) {
        std::string nKey = NormalizeKey(key);
        switch (level) {
        case ScopeLevel::Local:   m_local[nKey] = value; break;
        case ScopeLevel::Session: m_session[nKey] = value; break;
        case ScopeLevel::Global:  m_global[nKey] = value; break;
        }
    }

    std::optional<std::any> VariableScope::Get(const std::string& key) const {
        std::string nKey = NormalizeKey(key);

        // Local > Session > Global resolution order
        if (auto it = m_local.find(nKey); it != m_local.end()) return it->second;
        if (auto it = m_session.find(nKey); it != m_session.end()) return it->second;
        if (auto it = m_global.find(nKey); it != m_global.end()) return it->second;

        return std::nullopt;
    }

    bool VariableScope::Exists(const std::string& key) const {
        return Get(key).has_value();
    }

    ScopeLevel VariableScope::GetLevel(const std::string& key) const {
        std::string nKey = NormalizeKey(key);
        if (m_local.contains(nKey)) return ScopeLevel::Local;
        if (m_session.contains(nKey)) return ScopeLevel::Session;
        return ScopeLevel::Global;
    }

    bool VariableScope::PromoteToGlobal(const std::string& key) {
        auto val = Get(key);
        if (!val.has_value()) return false;

        std::string nKey = NormalizeKey(key);
        m_global[nKey] = val.value();
        m_local.erase(nKey);
        m_session.erase(nKey);
        return true;
    }

    void VariableScope::ClearLocal() {
        m_local.clear();
    }

}

/*
Implementation of `VariableScope`.
Enforces the scope hierarchy: Local shadows Session shadows Global.
Supports explicit promotion to global scope with warning capability.
(spec: "Promotion from local to global scope is allowed but must be explicit")
*/