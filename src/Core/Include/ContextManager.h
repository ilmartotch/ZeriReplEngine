#pragma once
#include <string>
#include <stack>
#include <optional>

namespace Zeri::Core {

    struct ExecutionContext {
        std::string name;
        std::string activatedBy;
        bool allowsExpressions{ true };
        bool allowsCommands{ true };
    };

    class ContextManager {
    public:
        void Push(const ExecutionContext& ctx);
        void Pop();
        [[nodiscard]] std::optional<ExecutionContext> Current() const;
        [[nodiscard]] bool IsEmpty() const;
        void Clear();

    private:
        std::stack<ExecutionContext> m_contextStack;
    };

}

/*
Manages the execution context stack as per meta-language specification.
(Command Model and Context Activation)
Once a context is active, subsequent input lines are interpreted according
to that context's rules until the context is exited or replaced.
The context stack allows nested contexts with proper push/pop semantics.
*/