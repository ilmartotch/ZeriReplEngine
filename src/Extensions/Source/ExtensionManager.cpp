#include "../Include/ExtensionManager.h"

namespace Zeri::Extensions {

    void ExtensionManager::RegisterExecutor(std::unique_ptr<Zeri::Engines::IExecutor> executor) {
        m_executors[executor->GetType()] = std::move(executor);
    }

    Zeri::Engines::IExecutor* ExtensionManager::GetExecutor(Zeri::Engines::ExecutionType type) {
        auto it = m_executors.find(type);
        if (it != m_executors.end()) {
            return it->second.get();
        }
        return nullptr;
    }

}

/*
Implementation of `ExtensionManager`.
Manages the lifecycle (ownership via unique_ptr) of registered executors.
*/
