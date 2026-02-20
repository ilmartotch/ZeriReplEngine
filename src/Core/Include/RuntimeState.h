#pragma once

#include <string>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <any>
#include <vector>
#include <memory>
#include <functional>
#include <optional>
#include <atomic>
#include "ContextManager.h"
#include "../../Modules/Include/ModuleManager.h"
#include "../../Engines/Include/Interface/IContext.h"

namespace Zeri::Core {

    class RuntimeState {
    public:
        enum class VariableScope {
            Local,
            Session,
            Global,
            Persisted
        };

        enum class OverwritePolicy {
            Overwrite,
            SkipIfExists,
            Merge
        };

        using FunctionSignature = std::function<double(const std::vector<double>&)>;

        RuntimeState();
        ~RuntimeState() = default;

        void SetVariable(VariableScope scope, const std::string& key, const std::any& value);
        bool SetVariable(VariableScope scope, const std::string& key, const std::any& value, OverwritePolicy policy);
        [[nodiscard]] std::any GetVariable(VariableScope scope, const std::string& key) const;
        [[nodiscard]] bool HasVariable(VariableScope scope, const std::string& key) const;

        void SetVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetVariable(const std::string& key) const;
        [[nodiscard]] bool HasVariable(const std::string& key) const;

        bool PromoteVariable(const std::string& key, VariableScope targetScope, OverwritePolicy policy = OverwritePolicy::Overwrite);

        void SetGlobalVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetGlobalVariable(const std::string& key) const;
        [[nodiscard]] bool HasGlobalVariable(const std::string& key) const;

        void SetSessionVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetSessionVariable(const std::string& key) const;
        [[nodiscard]] bool HasSessionVariable(const std::string& key) const;

        void SetPersistedVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetPersistedVariable(const std::string& key) const;
        [[nodiscard]] bool HasPersistedVariable(const std::string& key) const;

        bool SetFunction(VariableScope scope, const std::string& name, FunctionSignature function, OverwritePolicy policy = OverwritePolicy::Overwrite);
        [[nodiscard]] std::optional<FunctionSignature> GetFunction(VariableScope scope, const std::string& name) const;
        [[nodiscard]] std::optional<FunctionSignature> GetFunction(const std::string& name) const;
        [[nodiscard]] bool HasFunction(VariableScope scope, const std::string& name) const;
        [[nodiscard]] bool HasFunction(const std::string& name) const;
        bool PromoteFunction(const std::string& name, VariableScope targetScope, OverwritePolicy policy = OverwritePolicy::Overwrite);

        void SetGlobalFunction(const std::string& name, FunctionSignature function);
        void SetSessionFunction(const std::string& name, FunctionSignature function);
        void SetPersistedFunction(const std::string& name, FunctionSignature function);

        [[nodiscard]] std::map<std::string, FunctionSignature> GetResolvedFunctions() const;
        [[nodiscard]] std::size_t GetFunctionRegistryRevision() const;

        void SetActiveContext(const std::string& contextName);
        [[nodiscard]] std::string GetActiveContext() const;

        void PushContext(Zeri::Engines::ContextPtr context);
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
        std::map<std::string, std::any> m_persistedVariables;
        mutable std::shared_mutex m_varMutex;

        std::vector<std::map<std::string, FunctionSignature>> m_localFunctions;
        std::map<std::string, FunctionSignature> m_sessionFunctions;
        std::map<std::string, FunctionSignature> m_globalFunctions;
        std::map<std::string, FunctionSignature> m_persistedFunctions;
        mutable std::shared_mutex m_functionMutex;
        std::atomic_size_t m_functionRevision{ 0 };

        std::string m_activeContext;
        mutable std::shared_mutex m_activeContextMutex;

        std::vector<Zeri::Engines::ContextPtr> m_contextStack;
        Zeri::Core::ContextManager m_contextManager;
        mutable std::mutex m_stackMutex;

        bool m_exitRequested{ false };
        mutable std::mutex m_lifecycleMutex;

        Zeri::Modules::ModuleManager m_moduleManager;
    };

}