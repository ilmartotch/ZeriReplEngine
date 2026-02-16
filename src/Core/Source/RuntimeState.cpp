#include "RuntimeState.h"
#include "../../Engines/Include/Interface/IContext.h"

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
        std::unique_lock lock(m_varMutex);
        switch (scope) {
        case VariableScope::Local:
            if (m_localVariables.empty()) {
                m_localVariables.emplace_back();
            }
            m_localVariables.back()[key] = value;
            break;
        case VariableScope::Session:
            m_sessionVariables[key] = value;
            break;
        case VariableScope::Global:
            m_globalVariables[key] = value;
            break;
        }
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
        return m_globalVariables.contains(key);
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

    void RuntimeState::SetActiveContext(const std::string& contextName) {
        std::unique_lock lock(m_activeContextMutex);
        m_activeContext = contextName;
    }

    std::string RuntimeState::GetActiveContext() const {
        std::shared_lock lock(m_activeContextMutex);
        return m_activeContext;
    }

    void RuntimeState::PushContext(Zeri::Engines::ContextPtr context) {
        std::scoped_lock lock(m_stackMutex, m_varMutex);
        const auto newContextName = context ? context->GetName() : "unknown";
        const auto* current = m_contextStack.empty() ? nullptr : m_contextStack.back().get();

        ExecutionContext meta{};
        meta.name = newContextName;
        meta.activatedBy = current ? current->GetName() : "system";

        m_contextStack.push_back(std::move(context));
        m_contextManager.Push(meta);
        m_localVariables.emplace_back();
    }

    void RuntimeState::PopContext() {
        std::scoped_lock lock(m_stackMutex, m_varMutex);
        if (m_contextStack.size() > 1) {
            m_contextStack.pop_back();
            m_contextManager.Pop();
            if (m_localVariables.size() > 1) {
                m_localVariables.pop_back();
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