#pragma once

#include <string>
#include <map>
#include <shared_mutex>
#include <mutex>
#include <any>
#include <vector>
#include <memory>
#include "../../Modules/Include/ModuleManager.h"

namespace Zeri::Engines {
    class IContext;
}

namespace Zeri::Core {

    class RuntimeState {
    public:
        RuntimeState(); // Constructor needed now
        ~RuntimeState() = default;

        void SetGlobalVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetGlobalVariable(const std::string& key) const;
        [[nodiscard]] bool HasGlobalVariable(const std::string& key) const;

        void SetVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetVariable(const std::string& key) const;

        void SetActiveContext(const std::string& contextName);
        [[nodiscard]] std::string GetActiveContext() const;

        void PushContext(std::unique_ptr<Zeri::Engines::IContext> context);
        void PopContext();
        [[nodiscard]] Zeri::Engines::IContext* GetCurrentContext() const;
        [[nodiscard]] bool HasContexts() const;

        void RequestExit();
        [[nodiscard]] bool IsExitRequested() const;

        // --- Module Management ---
        [[nodiscard]] Zeri::Modules::ModuleManager& GetModuleManager();

    private:
        std::map<std::string, std::any> m_globalVariables;
        mutable std::shared_mutex m_varMutex;

        std::string m_activeContext;
        mutable std::shared_mutex m_activeContextMutex;

        std::vector<std::unique_ptr<Zeri::Engines::IContext>> m_contextStack;
        mutable std::mutex m_stackMutex;

        bool m_exitRequested{ false };
        mutable std::mutex m_lifecycleMutex;

        Zeri::Modules::ModuleManager m_moduleManager;
    };

}