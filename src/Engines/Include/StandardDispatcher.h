#pragma once
#include "Interface/IDispatcher.h"

namespace Zeri::Engines::Defaults {

    class StandardDispatcher : public IDispatcher {
    public:
        [[nodiscard]] ExecutionType Classify(const Command& cmd) override;
    };

}

/*
Header for `StandardDispatcher`.
Default routing logic for the REPL.
*/
