#pragma once

#include <string>
#include <memory>
#include "../Command.h"
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
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) = 0;

        [[nodiscard]] virtual bool WantsRawInput() const {
            return false;
        }

        [[nodiscard]] virtual ExecutionOutcome HandleRawLine(
            const std::string& line,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) {
            Command rawCommand;
            rawCommand.rawInput = line;
            rawCommand.type = InputType::Expression;
            return HandleCommand(rawCommand, state, terminal);
        }

        [[nodiscard]] virtual bool IsGlobalCommand(const std::string& name) const = 0;
    };

    using ContextPtr = std::unique_ptr<IContext>;

}

/*
Each instance represents a specific logical engine (e.g., $math, $global).
- HandleCommand: Process local commands.
- WantsRawInput/HandleRawLine: Optional polymorphic path for raw editor-like input.
- OnEnter/OnExit: Lifecycle hooks for wizards or welcome messages.
- ITerminal is passed to allow interactive "Wizards" (y/n questions) within the execution.
*/
