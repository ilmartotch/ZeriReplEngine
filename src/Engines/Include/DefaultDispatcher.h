#pragma once
#include "Interface/IDispatcher.h"

namespace Zeri::Engines::Defaults {

    class DefaultDispatcher final : public IDispatcher {
    public:
        [[nodiscard]] ExecutionType Classify(const Command& cmd) override;
    };
}