#pragma once
#include "../../Engines/Include/Interface/IDispatcher.h"
#include "../../Engines/Include/Interface/IExecutor.h"
#include <memory>
#include <map>

namespace Zeri::Extensions {

    class ExtensionManager {
    public:
        void RegisterExecutor(std::unique_ptr<Zeri::Engines::IExecutor> executor);
        [[nodiscard]] Zeri::Engines::IExecutor* GetExecutor(Zeri::Engines::ExecutionType type);

    private:
        std::map<Zeri::Engines::ExecutionType, std::unique_ptr<Zeri::Engines::IExecutor>> m_executors;
    };

}

/*
Header for `ExtensionManager`.
Acts as the central registry for all available executors.
This decoupling allows the main application to be unaware of specific executor implementations,
relying only on the `ExecutionType` enum to request the correct handler.
*/
