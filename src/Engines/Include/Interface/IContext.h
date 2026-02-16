#pragma once

#include <string>
#include <memory>
#include "ExecutionResult.h"
#include "../../Ui/Include/ITerminal.h"

namespace Zeri::Core { class RuntimeState; }

namespace Zeri::Engines {

    class IContext {
    public:
        virtual ~IContext() = default;

        virtual void OnEnter(Zeri::Ui::ITerminal& terminal) = 0;
        virtual void OnExit(Zeri::Ui::ITerminal& terminal) = 0;

        [[nodiscard]] virtual std::string GetName() const = 0;
        [[nodiscard]] virtual std::string GetPrompt() const = 0;

        [[nodiscard]] virtual ExecutionOutcome HandleCommand(
            const std::string& commandName,
            const std::vector<std::string>& args,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) = 0;

        [[nodiscard]] virtual bool IsGlobalCommand(const std::string& name) const = 0;
    };

    using ContextPtr = std::unique_ptr<IContext>;

}

/*
Each instance represents a specific logical engine (e.g., $math, $global).
- HandleCommand: Process local commands.
- OnEnter/OnExit: Lifecycle hooks for wizards or welcome messages.
- ITerminal is passed to allow interactive "Wizards" (y/n questions) within the execution.
*/
