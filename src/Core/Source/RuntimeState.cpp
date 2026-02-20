#include "RuntimeState.h"
#include "../../Engines/Include/Interface/IContext.h"

namespace {

    using Zeri::Core::RuntimeState;
    using OverwritePolicy = RuntimeState::OverwritePolicy;

    [[nodiscard]] bool MergeAnyValue(std::any& current, const std::any& incoming) {
        if (current.type() == typeid(std::vector<std::any>) &&
            incoming.type() == typeid(std::vector<std::any>)) {
            auto& currentVec = std::any_cast<std::vector<std::any>&>(current);
            const auto& incomingVec = std::any_cast<const std::vector<std::any>&>(incoming);
            currentVec.insert(currentVec.end(), incomingVec.begin(), incomingVec.end());
            return true;
        }

        if (current.type() == typeid(std::map<std::string, std::any>) &&
            incoming.type() == typeid(std::map<std::string, std::any>)) {
            auto& currentMap = std::any_cast<std::map<std::string, std::any>&>(current);
            const auto& incomingMap = std::any_cast<const std::map<std::string, std::any>&>(incoming);
            for (const auto& [key, value] : incomingMap) {
                if (!currentMap.contains(key)) {
                    currentMap.emplace(key, value);
                }
            }
            return true;
        }

        return false;
    }

    [[nodiscard]] bool ApplyVariablePolicy(
        std::map<std::string, std::any>& target,
        const std::string& key,
        const std::any& value,
        OverwritePolicy policy) {
        auto it = target.find(key);
        switch (policy) {
        case OverwritePolicy::Overwrite:
            target[key] = value;
            return true;
        case OverwritePolicy::SkipIfExists:
            if (it == target.end()) {
                target.emplace(key, value);
                return true;
            }
            return false;
        case OverwritePolicy::Merge:
            if (it == target.end()) {
                target.emplace(key, value);
                return true;
            }
            if (MergeAnyValue(it->second, value)) {
                return true;
            }
            it->second = value;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ApplyFunctionPolicy(
        std::map<std::string, RuntimeState::FunctionSignature>& target,
        const std::string& key,
        RuntimeState::FunctionSignature function,
        OverwritePolicy policy) {
        auto it = target.find(key);
        switch (policy) {
        case OverwritePolicy::Overwrite:
            target[key] = std::move(function);
            return true;
        case OverwritePolicy::SkipIfExists:
            if (it == target.end()) {
                target.emplace(key, std::move(function));
                return true;
            }
            return false;
        case OverwritePolicy::Merge:
            if (it == target.end()) {
                target.emplace(key, std::move(function));
                return true;
            }
            return false;
        }
        return false;
    }

}

namespace Zeri::Core {

    RuntimeState::RuntimeState() {
        m_moduleManager.StartBackgroundScan();
    }

    Zeri::Modules::ModuleManager& RuntimeState::GetModuleManager() {
        return m_moduleManager;
    }

    Zeri::Core::ContextManager& RuntimeState::GetContextManager() {
        return m_contextManager;
    }

    void RuntimeState::SetVariable(VariableScope scope, const std::string& key, const std::any& value) {
        SetVariable(scope, key, value, OverwritePolicy::Overwrite);
    }

    bool RuntimeState::SetVariable(VariableScope scope, const std::string& key, const std::any& value, OverwritePolicy policy) {
        std::unique_lock lock(m_varMutex);
        switch (scope) {
        case VariableScope::Local:
            if (m_localVariables.empty()) {
                m_localVariables.emplace_back();
            }
            return ApplyVariablePolicy(m_localVariables.back(), key, value, policy);
        case VariableScope::Session:
            return ApplyVariablePolicy(m_sessionVariables, key, value, policy);
        case VariableScope::Global:
            return ApplyVariablePolicy(m_globalVariables, key, value, policy);
        case VariableScope::Persisted:
            return ApplyVariablePolicy(m_persistedVariables, key, value, policy);
        }
        return false;
    }

    std::any RuntimeState::GetVariable(VariableScope scope, const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        switch (scope) {
        case VariableScope::Local:
            if (!m_localVariables.empty()) {
                auto it = m_localVariables.back().find(key);
                if (it != m_localVariables.back().end()) {
                    return it->second;
                }
            }
            return {};
        case VariableScope::Session: {
            auto it = m_sessionVariables.find(key);
            return it != m_sessionVariables.end() ? it->second : std::any{};
        }
        case VariableScope::Global: {
            auto it = m_globalVariables.find(key);
            return it != m_globalVariables.end() ? it->second : std::any{};
        }
        case VariableScope::Persisted: {
            auto it = m_persistedVariables.find(key);
            return it != m_persistedVariables.end() ? it->second : std::any{};
        }
        }
        return {};
    }

    bool RuntimeState::HasVariable(VariableScope scope, const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        switch (scope) {
        case VariableScope::Local:
            return !m_localVariables.empty() && m_localVariables.back().contains(key);
        case VariableScope::Session:
            return m_sessionVariables.contains(key);
        case VariableScope::Global:
            return m_globalVariables.contains(key);
        case VariableScope::Persisted:
            return m_persistedVariables.contains(key);
        }
        return false;
    }

    void RuntimeState::SetVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Local, key, value);
    }

    std::any RuntimeState::GetVariable(const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        if (!m_localVariables.empty()) {
            auto it = m_localVariables.back().find(key);
            if (it != m_localVariables.back().end()) {
                return it->second;
            }
        }

        auto sessionIt = m_sessionVariables.find(key);
        if (sessionIt != m_sessionVariables.end()) {
            return sessionIt->second;
        }

        auto globalIt = m_globalVariables.find(key);
        if (globalIt != m_globalVariables.end()) {
            return globalIt->second;
        }

        auto persistedIt = m_persistedVariables.find(key);
        if (persistedIt != m_persistedVariables.end()) {
            return persistedIt->second;
        }

        return {};
    }

    bool RuntimeState::HasVariable(const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        if (!m_localVariables.empty() && m_localVariables.back().contains(key)) {
            return true;
        }
        if (m_sessionVariables.contains(key)) {
            return true;
        }
        if (m_globalVariables.contains(key)) {
            return true;
        }
        return m_persistedVariables.contains(key);
    }

    bool RuntimeState::PromoteVariable(const std::string& key, VariableScope targetScope, OverwritePolicy policy) {
        if (targetScope == VariableScope::Local) {
            return false;
        }

        std::unique_lock lock(m_varMutex);
        if (m_localVariables.empty()) {
            return false;
        }

        auto& local = m_localVariables.back();
        auto it = local.find(key);
        if (it == local.end()) {
            return false;
        }

        const auto value = it->second;
        bool updated = false;

        switch (targetScope) {
        case VariableScope::Session:
            updated = ApplyVariablePolicy(m_sessionVariables, key, value, policy);
            break;
        case VariableScope::Global:
            updated = ApplyVariablePolicy(m_globalVariables, key, value, policy);
            break;
        case VariableScope::Persisted:
            updated = ApplyVariablePolicy(m_persistedVariables, key, value, policy);
            break;
        default:
            break;
        }

        if (!updated) {
            return false;
        }

        local.erase(it);
        return true;
    }

    void RuntimeState::SetGlobalVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Global, key, value);
    }

    std::any RuntimeState::GetGlobalVariable(const std::string& key) const {
        return GetVariable(VariableScope::Global, key);
    }

    bool RuntimeState::HasGlobalVariable(const std::string& key) const {
        return HasVariable(VariableScope::Global, key);
    }

    void RuntimeState::SetSessionVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Session, key, value);
    }

    std::any RuntimeState::GetSessionVariable(const std::string& key) const {
        return GetVariable(VariableScope::Session, key);
    }

    bool RuntimeState::HasSessionVariable(const std::string& key) const {
        return HasVariable(VariableScope::Session, key);
    }

    void RuntimeState::SetPersistedVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Persisted, key, value);
    }

    std::any RuntimeState::GetPersistedVariable(const std::string& key) const {
        return GetVariable(VariableScope::Persisted, key);
    }

    bool RuntimeState::HasPersistedVariable(const std::string& key) const {
        return HasVariable(VariableScope::Persisted, key);
    }

    bool RuntimeState::SetFunction(VariableScope scope, const std::string& name, FunctionSignature function, OverwritePolicy policy) {
        std::unique_lock lock(m_functionMutex);
        bool updated = false;

        switch (scope) {
        case VariableScope::Local:
            if (m_localFunctions.empty()) {
                m_localFunctions.emplace_back();
            }
            updated = ApplyFunctionPolicy(m_localFunctions.back(), name, std::move(function), policy);
            break;
        case VariableScope::Session:
            updated = ApplyFunctionPolicy(m_sessionFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Global:
            updated = ApplyFunctionPolicy(m_globalFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Persisted:
            updated = ApplyFunctionPolicy(m_persistedFunctions, name, std::move(function), policy);
            break;
        }

        if (updated) {
            ++m_functionRevision;
        }

        return updated;
    }

    std::optional<RuntimeState::FunctionSignature> RuntimeState::GetFunction(VariableScope scope, const std::string& name) const {
        std::shared_lock lock(m_functionMutex);
        switch (scope) {
        case VariableScope::Local:
            if (!m_localFunctions.empty()) {
                auto it = m_localFunctions.back().find(name);
                if (it != m_localFunctions.back().end()) {
                    return it->second;
                }
            }
            return std::nullopt;
        case VariableScope::Session: {
            auto it = m_sessionFunctions.find(name);
            return it != m_sessionFunctions.end() ? std::optional<FunctionSignature>{ it->second } : std::nullopt;
        }
        case VariableScope::Global: {
            auto it = m_globalFunctions.find(name);
            return it != m_globalFunctions.end() ? std::optional<FunctionSignature>{ it->second } : std::nullopt;
        }
        case VariableScope::Persisted: {
            auto it = m_persistedFunctions.find(name);
            return it != m_persistedFunctions.end() ? std::optional<FunctionSignature>{ it->second } : std::nullopt;
        }
        }
        return std::nullopt;
    }

    std::optional<RuntimeState::FunctionSignature> RuntimeState::GetFunction(const std::string& name) const {
        std::shared_lock lock(m_functionMutex);

        if (!m_localFunctions.empty()) {
            auto it = m_localFunctions.back().find(name);
            if (it != m_localFunctions.back().end()) {
                return it->second;
            }
        }

        auto sessionIt = m_sessionFunctions.find(name);
        if (sessionIt != m_sessionFunctions.end()) {
            return sessionIt->second;
        }

        auto globalIt = m_globalFunctions.find(name);
        if (globalIt != m_globalFunctions.end()) {
            return globalIt->second;
        }

        auto persistedIt = m_persistedFunctions.find(name);
        if (persistedIt != m_persistedFunctions.end()) {
            return persistedIt->second;
        }

        return std::nullopt;
    }

    bool RuntimeState::HasFunction(VariableScope scope, const std::string& name) const {
        std::shared_lock lock(m_functionMutex);
        switch (scope) {
        case VariableScope::Local:
            return !m_localFunctions.empty() && m_localFunctions.back().contains(name);
        case VariableScope::Session:
            return m_sessionFunctions.contains(name);
        case VariableScope::Global:
            return m_globalFunctions.contains(name);
        case VariableScope::Persisted:
            return m_persistedFunctions.contains(name);
        }
        return false;
    }

    bool RuntimeState::HasFunction(const std::string& name) const {
        std::shared_lock lock(m_functionMutex);
        if (!m_localFunctions.empty() && m_localFunctions.back().contains(name)) {
            return true;
        }
        if (m_sessionFunctions.contains(name)) {
            return true;
        }
        if (m_globalFunctions.contains(name)) {
            return true;
        }
        return m_persistedFunctions.contains(name);
    }

    bool RuntimeState::PromoteFunction(const std::string& name, VariableScope targetScope, OverwritePolicy policy) {
        if (targetScope == VariableScope::Local) {
            return false;
        }

        std::unique_lock lock(m_functionMutex);
        if (m_localFunctions.empty()) {
            return false;
        }

        auto& local = m_localFunctions.back();
        auto it = local.find(name);
        if (it == local.end()) {
            return false;
        }

        auto function = it->second;
        bool updated = false;

        switch (targetScope) {
        case VariableScope::Session:
            updated = ApplyFunctionPolicy(m_sessionFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Global:
            updated = ApplyFunctionPolicy(m_globalFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Persisted:
            updated = ApplyFunctionPolicy(m_persistedFunctions, name, std::move(function), policy);
            break;
        default:
            break;
        }

        if (!updated) {
            return false;
        }

        local.erase(it);
        ++m_functionRevision;
        return true;
    }

    void RuntimeState::SetGlobalFunction(const std::string& name, FunctionSignature function) {
        SetFunction(VariableScope::Global, name, std::move(function));
    }

    void RuntimeState::SetSessionFunction(const std::string& name, FunctionSignature function) {
        SetFunction(VariableScope::Session, name, std::move(function));
    }

    void RuntimeState::SetPersistedFunction(const std::string& name, FunctionSignature function) {
        SetFunction(VariableScope::Persisted, name, std::move(function));
    }

    std::map<std::string, RuntimeState::FunctionSignature> RuntimeState::GetResolvedFunctions() const {
        std::shared_lock lock(m_functionMutex);
        std::map<std::string, FunctionSignature> resolved;

        for (const auto& [name, function] : m_persistedFunctions) {
            resolved[name] = function;
        }
        for (const auto& [name, function] : m_globalFunctions) {
            resolved[name] = function;
        }
        for (const auto& [name, function] : m_sessionFunctions) {
            resolved[name] = function;
        }
        if (!m_localFunctions.empty()) {
            for (const auto& [name, function] : m_localFunctions.back()) {
                resolved[name] = function;
            }
        }

        return resolved;
    }

    std::size_t RuntimeState::GetFunctionRegistryRevision() const {
        return m_functionRevision.load();
    }

    void RuntimeState::SetActiveContext(const std::string& contextName) {
        std::unique_lock lock(m_activeContextMutex);
        m_activeContext = contextName;
    }

    std::string RuntimeState::GetActiveContext() const {
        std::shared_lock lock(m_activeContextMutex);
        return m_activeContext;
    }

    void RuntimeState::PushContext(Zeri::Engines::ContextPtr context) {
        std::scoped_lock lock(m_stackMutex, m_varMutex, m_functionMutex);
        const auto newContextName = context ? context->GetName() : "unknown";
        const auto* current = m_contextStack.empty() ? nullptr : m_contextStack.back().get();

        ExecutionContext meta{};
        meta.name = newContextName;
        meta.activatedBy = current ? current->GetName() : "system";

        m_contextStack.push_back(std::move(context));
        m_contextManager.Push(meta);
        m_localVariables.emplace_back();
        m_localFunctions.emplace_back();
    }

    void RuntimeState::PopContext() {
        std::scoped_lock lock(m_stackMutex, m_varMutex, m_functionMutex);
        if (m_contextStack.size() > 1) {
            m_contextStack.pop_back();
            m_contextManager.Pop();
            if (m_localVariables.size() > 1) {
                m_localVariables.pop_back();
            }
            if (m_localFunctions.size() > 1) {
                m_localFunctions.pop_back();
            }
        }
    }

    Zeri::Engines::IContext* RuntimeState::GetCurrentContext() const {
        std::lock_guard<std::mutex> lock(m_stackMutex);
        if (m_contextStack.empty()) return nullptr;
        return m_contextStack.back().get();
    }

    bool RuntimeState::HasContexts() const {
        std::lock_guard<std::mutex> lock(m_stackMutex);
        return !m_contextStack.empty();
    }

    void RuntimeState::RequestExit() {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        m_exitRequested = true;
    }

    bool RuntimeState::IsExitRequested() const {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        return m_exitRequested;
    }

}