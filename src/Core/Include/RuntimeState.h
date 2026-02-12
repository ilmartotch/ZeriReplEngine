#pragma once

#include <string>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <any>
#include <vector>
#include <memory>
#include "ContextManager.h"
#include "../../Modules/Include/ModuleManager.h"

namespace Zeri::Engines {
    class IContext;
}

namespace Zeri::Core {

    class RuntimeState {
    public:
        enum class VariableScope {
            Local,
            Session,
            Global
        };

        RuntimeState();
        ~RuntimeState() = default;

        void SetVariable(VariableScope scope, const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetVariable(VariableScope scope, const std::string& key) const;
        [[nodiscard]] bool HasVariable(VariableScope scope, const std::string& key) const;

        void SetVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetVariable(const std::string& key) const;
        [[nodiscard]] bool HasVariable(const std::string& key) const;

        void SetGlobalVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetGlobalVariable(const std::string& key) const;
        [[nodiscard]] bool HasGlobalVariable(const std::string& key) const;

        void SetSessionVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetSessionVariable(const std::string& key) const;
        [[nodiscard]] bool HasSessionVariable(const std::string& key) const;

        void SetActiveContext(const std::string& contextName);
        [[nodiscard]] std::string GetActiveContext() const;

        void PushContext(std::unique_ptr<Zeri::Engines::IContext> context);
        void PopContext();
        [[nodiscard]] Zeri::Engines::IContext* GetCurrentContext() const;
        [[nodiscard]] bool HasContexts() const;

        void RequestExit();
        [[nodiscard]] bool IsExitRequested() const;

        [[nodiscard]] Zeri::Modules::ModuleManager& GetModuleManager();
        [[nodiscard]] Zeri::Core::ContextManager& GetContextManager();

    private:
        std::vector<std::map<std::string, std::any>> m_localVariables;
        std::map<std::string, std::any> m_sessionVariables;
        std::map<std::string, std::any> m_globalVariables;
        mutable std::shared_mutex m_varMutex;

        std::string m_activeContext;
        mutable std::shared_mutex m_activeContextMutex;

        std::vector<std::unique_ptr<Zeri::Engines::IContext>> m_contextStack;
        Zeri::Core::ContextManager m_contextManager;
        mutable std::mutex m_stackMutex;

        bool m_exitRequested{ false };
        mutable std::mutex m_lifecycleMutex;

        Zeri::Modules::ModuleManager m_moduleManager;
    };

}