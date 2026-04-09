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
Definisce un contesto editor modale language-agnostic che accumula input grezzo
in memoria e delega l'esecuzione finale a un IExecutor fornito dal chiamante.
Espone i comandi interni /run, /save e /cancel, mantenendo il contesto generico
riutilizzabile per runtime differenti senza dipendenze dal parser specifico.
*/
