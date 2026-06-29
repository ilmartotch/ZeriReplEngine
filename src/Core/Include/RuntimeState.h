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
#include <expected>
#include <filesystem>
#include <nlohmann/json.hpp>
#include "ContextManager.h"

namespace Zeri::Engines {
    class IContext;
    using ContextPtr = std::unique_ptr<IContext>;
}

namespace Zeri::Modules {
    class ModuleManager;
}

namespace Zeri::Core {

    using AnyValue = std::any;

    class RuntimeState {
    public:
        struct MathFunctionDefinition {
            std::vector<std::string> params;
            std::string expression;
        };

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
        ~RuntimeState();

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
        void SetMathVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::map<std::string, std::any> GetMathVariables() const;

        [[nodiscard]] std::optional<AnyValue> GetShared(const std::string& key) const;
        void SetShared(const std::string& key, const AnyValue& value);
        [[nodiscard]] std::vector<std::pair<std::string, AnyValue>> ListShared() const;
        void DeleteShared(const std::string& key);
        void ClearShared();

        [[nodiscard]] static std::optional<nlohmann::json> SerializeAnyValue(const AnyValue& value);
        [[nodiscard]] static std::optional<AnyValue> DeserializeAnyValue(const nlohmann::json& value);
        [[nodiscard]] static std::string DescribeAnyValueType(const AnyValue& value);

        bool SetFunction(VariableScope scope, const std::string& name, FunctionSignature function, OverwritePolicy policy = OverwritePolicy::Overwrite);
        [[nodiscard]] std::optional<FunctionSignature> GetFunction(VariableScope scope, const std::string& name) const;
        [[nodiscard]] std::optional<FunctionSignature> GetFunction(const std::string& name) const;
        [[nodiscard]] bool HasFunction(VariableScope scope, const std::string& name) const;
        [[nodiscard]] bool HasFunction(const std::string& name) const;
        bool PromoteFunction(const std::string& name, VariableScope targetScope, OverwritePolicy policy = OverwritePolicy::Overwrite);

        void SetGlobalFunction(const std::string& name, FunctionSignature function);
        void SetSessionFunction(const std::string& name, FunctionSignature function);
        void SetPersistedFunction(const std::string& name, FunctionSignature function);
        void SetMathFunctionDefinition(const std::string& name, MathFunctionDefinition definition);
        [[nodiscard]] std::map<std::string, MathFunctionDefinition> GetMathFunctionDefinitions() const;

        [[nodiscard]] std::map<std::string, FunctionSignature> GetResolvedFunctions() const;
        [[nodiscard]] std::size_t GetFunctionRegistryRevision() const;

        [[nodiscard]] std::map<std::string, std::any> GetCurrentLocalVariables() const;
        [[nodiscard]] std::map<std::string, FunctionSignature> GetCurrentLocalFunctions() const;

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

        [[nodiscard]] std::expected<void, std::string> SaveSession(const std::filesystem::path& path) const;

        [[nodiscard]] std::expected<void, std::string> LoadSession(const std::filesystem::path& path);
        [[nodiscard]] std::expected<nlohmann::json, std::string> ExportSessionState() const;
        [[nodiscard]] std::expected<void, std::string> ImportSessionState(const nlohmann::json& root);
        [[nodiscard]] bool WasSessionCorrupted() const;

        void ResetSession();

    private:
        std::vector<std::map<std::string, std::any>> m_localVariables;
        std::map<std::string, std::any> m_sessionVariables;
        std::map<std::string, std::any> m_globalVariables;
        std::map<std::string, std::any> m_persistedVariables;
        std::map<std::string, std::any> m_mathVariables;
        mutable std::shared_mutex m_varMutex;

        std::map<std::string, AnyValue> m_sharedVariables;
        mutable std::shared_mutex m_sharedMutex;

        std::vector<std::map<std::string, FunctionSignature>> m_localFunctions;
        std::map<std::string, FunctionSignature> m_sessionFunctions;
        std::map<std::string, FunctionSignature> m_globalFunctions;
        std::map<std::string, FunctionSignature> m_persistedFunctions;
        std::map<std::string, MathFunctionDefinition> m_mathFunctionDefinitions;
        mutable std::shared_mutex m_functionMutex;
        std::atomic_size_t m_functionRevision{ 0 };

        std::string m_activeContext;
        mutable std::shared_mutex m_activeContextMutex;

        Zeri::Core::ContextManager m_contextManager;

        bool m_exitRequested{ false };
        bool m_sessionCorrupted{ false };
        std::string m_startupWarning;
        mutable std::mutex m_lifecycleMutex;

        std::unique_ptr<Zeri::Modules::ModuleManager> m_moduleManager;
    };

}

/*
RuntimeState manages the entire state of the REPL session.
It holds variables and functions with different scopes (Local, Session, Global, Persisted),
and manages the context stack and lifecycle.
Engine context types are forward-declared in this header to avoid direct include coupling
between Core and the concrete engine interface definition.
ModuleManager is forward-declared to keep RuntimeState public declarations lightweight
and avoid importing module subsystem headers in Core includes.
RuntimeState owns ModuleManager through std::unique_ptr to preserve stable public APIs
while allowing forward declaration of the module subsystem type.

SaveSession:
Serialize the full executable runtime state to a JSON file on disk.
path: Destination file path (e.g. ".zeri/state.json").

LoadSession:
Load and replace the full executable runtime state from a JSON file on disk.
path: Source file path (e.g. ".zeri/state.json").
*/