#pragma once

#include "BaseContext.h"
#include "Interface/IExecutor.h"

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace Zeri::Engines {

    class ScriptEditorContext : public BaseContext {
    public:
        ScriptEditorContext(
            std::shared_ptr<IExecutor> executor,
            std::string language,
            std::optional<std::string> scriptName = std::nullopt,
            std::vector<std::string> initialBuffer = {}
        );

        [[nodiscard]] ExecutionOutcome HandleCommand(
            const Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

        [[nodiscard]] bool WantsRawInput() const override { return true; }

        [[nodiscard]] ExecutionOutcome HandleRawLine(
            const std::string& line,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override;

        [[nodiscard]] std::string GetName() const override { return "script_editor"; }
        [[nodiscard]] std::string GetPrompt() const override;
        void OnEnter(Zeri::Ui::ITerminal& terminal) override;
        void OnExit(Zeri::Ui::ITerminal& terminal) override;

    private:
        std::shared_ptr<IExecutor> m_executor;
        std::string m_language;
        std::optional<std::string> m_scriptName;
        std::vector<std::string> m_buffer;
    };

}

/*
ScriptEditorContext.h
Defines a language-agnostic modal editor context that accumulates raw input
in memory and delegates final execution to a caller-provided IExecutor.
Exposes built-in /run, /save, and /cancel commands, keeping the context generic
and reusable across different runtimes without parser-specific dependencies.
Exposes WantsRawInput/HandleRawLine to avoid downcasting in the REPL loop.
*/
