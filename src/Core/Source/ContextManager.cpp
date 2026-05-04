#include "../Include/ContextManager.h"
#include "../../Engines/Include/Interface/IContext.h"

namespace Zeri::Core {

    ContextManager::ContextManager() = default;

    ContextManager::~ContextManager() = default;

    void ContextManager::Push(Zeri::Engines::ContextPtr context) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (context != nullptr) {
            m_contextStack.push_back(std::move(context));
        }
    }

    void ContextManager::Pop() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_contextStack.size() > 1) {
            m_contextStack.pop_back();
        }
    }

    Zeri::Engines::IContext* ContextManager::Current() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_contextStack.empty()) {
            return nullptr;
        }
        return m_contextStack.back().get();
    }

    bool ContextManager::IsEmpty() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_contextStack.empty();
    }

    std::size_t ContextManager::Size() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_contextStack.size();
    }

    void ContextManager::Clear() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_contextStack.clear();
    }

}

/*
Implementation of `ContextManager` with unique ownership of engine contexts.
Push stores context instances by move, Pop preserves root context by keeping
at least one frame, Current exposes non-owning access to active context.
Each public operation is guarded by an internal mutex to provide thread-safe
stack access independent from caller-side lock policies.
*/