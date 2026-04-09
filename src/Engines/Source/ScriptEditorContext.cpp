#include "../Include/ScriptEditorContext.h"

#include "../Include/ScriptRegistry.h"

#include <cctype>
#include <expected>
#include <sstream>
#include <string_view>

namespace {

    [[nodiscard]] std::string Trim(std::string_view value) {
        size_t begin = 0;
        while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
            ++begin;
        }

        size_t end = value.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
            --end;
        }

        return std::string(value.substr(begin, end - begin));
    }

    [[nodiscard]] std::string JoinLines(const std::vector<std::string>& lines) {
        std::ostringstream stream;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) {
                stream << '\n';
            }
            stream << lines[i];
        }
        return stream.str();
    }

    [[nodiscard]] std::string BuildLastBufferKey(std::string_view language) {
        std::string key;
        key.reserve(language.size() + 21);
        key.append(language);
        key.append("::editor::last_buffer");
        return key;
    }

}

namespace Zeri::Engines {

    ScriptEditorContext::ScriptEditorContext(
        std::shared_ptr<IExecutor> executor,
        std::string language,
        std::optional<std::string> scriptName,
        std::vector<std::string> initialBuffer
    )
        : m_executor(std::move(executor))
        , m_language(std::move(language))
        , m_scriptName(std::move(scriptName))
        , m_buffer(std::move(initialBuffer)) {
    }

    ExecutionOutcome ScriptEditorContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string input = Trim(cmd.rawInput);

        if (input == "/run") {
            if (m_executor == nullptr) {
                terminal.WriteError("Executor non disponibile per ScriptEditorContext.");
                return std::unexpected(ExecutionError{ "EDITOR_EXECUTOR_MISSING", "Executor non disponibile." });
            }

            Command syntheticCommand;
            syntheticCommand.rawInput = JoinLines(m_buffer);
            state.SetSessionVariable(BuildLastBufferKey(m_language), syntheticCommand.rawInput);

            auto executionResult = m_executor->Execute(syntheticCommand, state, terminal);
            state.PopContext();
            return executionResult;
        }

        if (input == "/save") {
            if (!m_scriptName.has_value() || m_scriptName->empty()) {
                terminal.WriteError("/save richiede uno scriptName associato al context.");
                return std::unexpected(ExecutionError{
                    "EDITOR_SAVE_NAME_MISSING",
                    "Impossibile salvare: nome script assente.",
                    cmd.rawInput,
                    { "Apri l'editor con nome script e ripeti /save." }
                });
            }

            const std::string joinedBuffer = JoinLines(m_buffer);
            SaveScript(state, m_language, *m_scriptName, joinedBuffer);
            state.SetSessionVariable(BuildLastBufferKey(m_language), joinedBuffer);
            state.PopContext();
            return "Script salvato: " + *m_scriptName;
        }

        if (input == "/cancel") {
            m_buffer.clear();
            state.PopContext();
            return "Editor annullato.";
        }

        m_buffer.push_back(cmd.rawInput);
        terminal.WriteInfo("[" + std::to_string(m_buffer.size()) + "] " + cmd.rawInput);
        return "";
    }

    std::string ScriptEditorContext::GetPrompt() const {
        return "zeri::edit::" + m_language + ">";
    }

    void ScriptEditorContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("-- Editor attivo. /run per eseguire, /save per salvare, /cancel per uscire.");
    }

    void ScriptEditorContext::OnExit(Zeri::Ui::ITerminal& terminal) {
        m_buffer.clear();
        (void)terminal;
    }

}

/*
ScriptEditorContext.cpp
Implementa un editor modale genericamente riusabile che accumula linee raw senza
parse sintattico contestuale, con comandi interni /run, /save, /cancel.
/run crea un Command sintetico con rawInput joinato su newline e delega a IExecutor;
/save persiste il buffer via ScriptRegistry usando chiave "{lang}::scripts::{name}";
/cancel scarta lo stato locale. In tutti i percorsi terminali viene effettuato PopContext.
*/
