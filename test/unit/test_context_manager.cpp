#include "../../src/Core/Include/ContextManager.h"
#include "../../src/Engines/Include/Interface/IContext.h"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
    using Zeri::Core::ContextManager;
    using Zeri::Core::RuntimeState;
    using Zeri::Engines::Command;
    using Zeri::Engines::ExecutionMessage;
    using Zeri::Engines::ExecutionOutcome;
    using Zeri::Engines::IContext;
    using Zeri::Ui::ITerminal;

    int g_failures = 0;

    void Expect(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "[ContextManager] " << message << "\n";
            ++g_failures;
        }
    }

    class DummyTerminal final : public ITerminal {
    public:
        void Write(const std::string&) override {}
        void WriteLine(const std::string&) override {}
        void WriteError(const std::string&) override {}
        void WriteSuccess(const std::string&) override {}
        void WriteInfo(const std::string&) override {}
        [[nodiscard]] std::optional<std::string> ReadLine(const std::string&) override { return std::nullopt; }
        [[nodiscard]] bool Confirm(const std::string&, bool) override { return true; }
        [[nodiscard]] std::optional<int> SelectMenu(const std::string&, const std::vector<std::string>&) override { return std::nullopt; }
    };

    class StubContext final : public IContext {
    public:
        explicit StubContext(std::string id) : m_id(std::move(id)) {}

        void OnEnter(ITerminal&) override {}
        void OnExit(ITerminal&) override {}
        [[nodiscard]] std::string GetName() const override { return m_id; }
        [[nodiscard]] std::string GetPrompt() const override { return m_id + "> "; }
        [[nodiscard]] ExecutionOutcome HandleCommand(const Command&, RuntimeState&, ITerminal&) override {
            return ExecutionMessage{"ok"};
        }
        [[nodiscard]] bool IsGlobalCommand(const std::string&) const override { return false; }

    private:
        std::string m_id;
    };

    void TestPushIncreasesDepth() {
        ContextManager manager;
        const auto before = manager.Size();
        manager.Push(std::make_unique<StubContext>("root"));
        Expect(manager.Size() == before + 1, "Push should increase stack depth");
    }

    void TestPopDecreasesDepthAndRestoresPrevious() {
        ContextManager manager;
        manager.Push(std::make_unique<StubContext>("root"));
        manager.Push(std::make_unique<StubContext>("child"));

        IContext* current = manager.Current();
        Expect(current != nullptr, "current context should exist after push");
        if (current != nullptr) {
            Expect(current->GetName() == "child", "top context before pop should be child");
        }

        manager.Pop();
        current = manager.Current();
        Expect(manager.Size() == 1, "Pop should reduce depth when stack has more than one context");
        Expect(current != nullptr, "current context should exist after pop");
        if (current != nullptr) {
            Expect(current->GetName() == "root", "previous context should be active after pop");
        }
    }

    void TestBackFromRootNoCrash() {
        ContextManager manager;
        manager.Push(std::make_unique<StubContext>("root"));
        manager.Pop();
        Expect(manager.Size() == 1, "Pop on root should be no-op");
        IContext* current = manager.Current();
        Expect(current != nullptr, "root context should remain active after root pop");
        if (current != nullptr) {
            Expect(current->GetName() == "root", "root context should remain unchanged");
        }
    }

    void TestDoublePushOrder() {
        ContextManager manager;
        manager.Push(std::make_unique<StubContext>("root"));
        manager.Push(std::make_unique<StubContext>("mid"));
        manager.Push(std::make_unique<StubContext>("top"));

        IContext* current = manager.Current();
        Expect(current != nullptr, "current context should exist after double push");
        if (current != nullptr) {
            Expect(current->GetName() == "top", "stack top should be last pushed context");
        }

        manager.Pop();
        current = manager.Current();
        Expect(current != nullptr, "current context should exist after one pop");
        if (current != nullptr) {
            Expect(current->GetName() == "mid", "stack order should be LIFO after pop");
        }
    }

    void TestInvalidTransitionKeepsStackUnchanged() {
        ContextManager manager;
        manager.Push(std::make_unique<StubContext>("root"));
        const auto before = manager.Size();
        manager.Push(nullptr);
        Expect(manager.Size() == before, "pushing null context should not change stack");
        IContext* current = manager.Current();
        Expect(current != nullptr, "current context should still exist after invalid push");
        if (current != nullptr) {
            Expect(current->GetName() == "root", "invalid push should keep active context unchanged");
        }
    }
}

int main() {
    TestPushIncreasesDepth();
    TestPopDecreasesDepthAndRestoresPrevious();
    TestBackFromRootNoCrash();
    TestDoublePushOrder();
    TestInvalidTransitionKeepsStackUnchanged();

    if (g_failures > 0) {
        std::cerr << "[ContextManager] Failures: " << g_failures << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
