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
                terminal.WriteError("[ZERI][CONTEXT-008] Script editor executor is unavailable. Hint: re-enter the language context and retry /run.");
                return std::unexpected(ExecutionError{ "EDITOR_EXECUTOR_MISSING", "Executor unavailable." });
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
                terminal.WriteError("[ZERI][CONTEXT-009] /save requires a script name associated with the editor context. Hint: open the editor with /new <name> or /edit <name>.");
                return std::unexpected(ExecutionError{
                    "EDITOR_SAVE_NAME_MISSING",
                    "Unable to save: missing script name.",
                    cmd.rawInput,
                    { "Open the editor with a script name and run /save again." }
                });
            }

            const std::string joinedBuffer = JoinLines(m_buffer);
            SaveScript(state, m_language, *m_scriptName, joinedBuffer);
            state.SetSessionVariable(BuildLastBufferKey(m_language), joinedBuffer);
            state.PopContext();
            return "Script saved: " + *m_scriptName;
        }

        if (input == "/cancel") {
            m_buffer.clear();
            state.PopContext();
            return "Editor canceled.";
        }

        m_buffer.push_back(cmd.rawInput);
        terminal.WriteInfo("[" + std::to_string(m_buffer.size()) + "] " + cmd.rawInput);
        return "";
    }

    ExecutionOutcome ScriptEditorContext::HandleRawLine(
        const std::string& line,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        Command rawCommand;
        rawCommand.rawInput = line;
        rawCommand.type = InputType::Expression;
        return HandleCommand(rawCommand, state, terminal);
    }

    std::string ScriptEditorContext::GetPrompt() const {
        return "zeri::edit::" + m_language + ">";
    }

    void ScriptEditorContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        (void)terminal;
    }

    void ScriptEditorContext::OnExit(Zeri::Ui::ITerminal& terminal) {
        m_buffer.clear();
        (void)terminal;
    }

}

/*
ScriptEditorContext.cpp
Implements a reusable modal editor that accumulates raw lines without
context-specific parsing, with built-in /run, /save, and /cancel commands.
/run creates a synthetic Command with newline-joined rawInput and delegates to IExecutor.
/save persists the buffer through ScriptRegistry with key "{lang}::scripts::{name}".
/cancel discards local state. All terminal paths perform PopContext.
HandleRawLine allows the caller to send raw input via the IContext interface
without depending on RTTI checks in the main loop.
*/
