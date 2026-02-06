#include "../Include/ContextManager.h"

namespace Zeri::Core {

    void ContextManager::Push(const ExecutionContext& ctx) {
        m_contextStack.push(ctx);
    }

    void ContextManager::Pop() {
        if (!m_contextStack.empty()) {
            m_contextStack.pop();
        }
    }

    std::optional<ExecutionContext> ContextManager::Current() const {
        if (m_contextStack.empty()) {
            return std::nullopt;
        }
        return m_contextStack.top();
    }

    bool ContextManager::IsEmpty() const {
        return m_contextStack.empty();
    }

    void ContextManager::Clear() {
        while (!m_contextStack.empty()) {
            m_contextStack.pop();
        }
    }

}

/*
Implementation of `ContextManager`.
Stub implementation for context stack management.
Real implementation should enforce context-specific parsing rules.
*/