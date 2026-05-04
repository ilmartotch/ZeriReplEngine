#pragma once

#include <cstddef>
#include <mutex>
#include <memory>
#include <vector>

namespace Zeri::Engines {
    class IContext;
    using ContextPtr = std::unique_ptr<IContext>;
}

namespace Zeri::Core {

    class ContextManager {
    public:
        ContextManager();
        ~ContextManager();

        void Push(Zeri::Engines::ContextPtr context);
        void Pop();
        [[nodiscard]] Zeri::Engines::IContext* Current() const;
        [[nodiscard]] bool IsEmpty() const;
        [[nodiscard]] std::size_t Size() const;
        void Clear();

    private:
        mutable std::mutex m_mutex;
        std::vector<Zeri::Engines::ContextPtr> m_contextStack;
    };

}

/*
Manages the execution context stack as per meta-language specification.
(Command Model and Context Activation)
Once a context is active, subsequent input lines are interpreted according
to that context's rules until the context is exited or replaced.
The context stack owns engine context instances through unique pointers and
provides push/pop/current access for nested context semantics.
All operations are internally synchronized to prevent concurrent stack
mutation/read data races.
*/