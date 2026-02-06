#include "RuntimeState.h"
#include "../../Engines/Include/Interface/IContext.h"

namespace Zeri::Core {

    RuntimeState::RuntimeState() {
        // Start scanning immediately upon state initialization
        m_moduleManager.StartBackgroundScan();
    }

    Zeri::Modules::ModuleManager& RuntimeState::GetModuleManager() {
        return m_moduleManager;
    }

    void RuntimeState::SetGlobalVariable(const std::string& key, const std::any& value) {
        std::unique_lock lock(m_varMutex);
        m_globalVariables[key] = value;
    }

    std::any RuntimeState::GetGlobalVariable(const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        auto it = m_globalVariables.find(key);
        if (it != m_globalVariables.end()) {
            return it->second;
        }
        return {};
    }

    bool RuntimeState::HasGlobalVariable(const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        return m_globalVariables.contains(key);
    }

    void RuntimeState::PushContext(std::unique_ptr<Zeri::Engines::IContext> context) {
        std::lock_guard<std::mutex> lock(m_stackMutex);
        m_contextStack.push_back(std::move(context));
    }

    void RuntimeState::PopContext() {
        std::lock_guard<std::mutex> lock(m_stackMutex);
        if (m_contextStack.size() > 1) {
            m_contextStack.pop_back();
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