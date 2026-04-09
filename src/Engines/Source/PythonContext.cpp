#include "../Include/PythonContext.h"

#include "../Include/PythonExecutor.h"
#include "../Include/ScriptEditorContext.h"
#include "../Include/ScriptRegistry.h"
#include "../../Core/Include/SystemGuard.h"

#include <any>
#include <sstream>
#include <string_view>
#include <vector>

namespace {

    [[nodiscard]] std::string BuildLastBufferKey(std::string_view language) {
        std::string key;
        key.reserve(language.size() + 21);
        key.append(language);
        key.append("::editor::last_buffer");
        return key;
    }

    [[nodiscard]] std::optional<std::string> AnyToString(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value.type() == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
        return std::nullopt;
    }

    [[nodiscard]] std::vector<std::string> SplitLines(const std::string& text) {
        std::vector<std::string> lines;
        std::istringstream stream(text);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(std::move(line));
        }
        if (lines.empty()) {
            lines.push_back({});
        }
        return lines;
    }

    [[nodiscard]] std::string JoinArgs(const std::vector<std::string>& args) {
        std::string result;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                result.push_back(' ');
            }
            result.append(args[i]);
        }
        return result;
    }

}

namespace Zeri::Engines::Defaults {

    PythonContext::PythonContext() {
        const auto health = Zeri::Core::SystemGuard::CheckEnvironment();
        if (const auto* runtime = health.GetRuntime("python"); runtime != nullptr && runtime->available) {
            m_executor = std::make_shared<PythonExecutor>(*runtime);
        } else {
            Zeri::Core::ScriptRuntime missingRuntime;
            missingRuntime.language = "python";
            m_executor = std::make_shared<PythonExecutor>(missingRuntime);
        }
    }

    void PythonContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("Python context active. Use /new, /run, /list, /edit, /show, /delete.");
    }

    ExecutionOutcome PythonContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string lastBufferKey = BuildLastBufferKey("python");

        if (cmd.type == InputType::Expression) {
            Command execCmd;
            execCmd.rawInput = cmd.rawInput.empty() ? JoinArgs(cmd.args) : cmd.rawInput;
            state.SetSessionVariable(lastBufferKey, execCmd.rawInput);
            return m_executor->Execute(execCmd, state, terminal);
        }

        if (cmd.commandName == "new") {
            std::optional<std::string> scriptName;
            if (const auto saveIt = cmd.flags.find("save"); saveIt != cmd.flags.end()) {
                if (saveIt->second.empty() || saveIt->second == "true") {
                    return std::unexpected(ExecutionError{
                        "PYTHON_NEW_SAVE_NAME_MISSING",
                        "Missing script name for /new --save.",
                        cmd.rawInput,
                        { "Usage: /new --save <name>" }
                    });
                }
                scriptName = saveIt->second;
            }

            auto editor = std::make_unique<Zeri::Engines::ScriptEditorContext>(m_executor, "python", scriptName);
            state.PushContext(std::move(editor));

            auto* active = state.GetCurrentContext();
            if (active != nullptr) {
                active->OnEnter(terminal);
            }

            return scriptName.has_value()
                ? "Python editor opened for script: " + *scriptName
                : "Python editor opened.";
        }

        if (cmd.commandName == "run") {
            std::string code;
            if (!cmd.args.empty()) {
                const auto script = LoadScript(state, "python", cmd.args[0]);
                if (!script.has_value()) {
                    return std::unexpected(ExecutionError{ "PYTHON_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
                }
                code = *script;
            } else {
                const auto lastBufferAny = state.GetSessionVariable(lastBufferKey);
                const auto lastBuffer = AnyToString(lastBufferAny);
                if (!lastBuffer.has_value() || lastBuffer->empty()) {
                    return std::unexpected(ExecutionError{
                        "PYTHON_LAST_BUFFER_MISSING",
                        "No last Python buffer available.",
                        cmd.rawInput,
                        { "Use /new and /run first, or execute /run <name>." }
                    });
                }
                code = *lastBuffer;
            }

            state.SetSessionVariable(lastBufferKey, code);
            Command execCmd;
            execCmd.rawInput = code;
            return m_executor->Execute(execCmd, state, terminal);
        }

        if (cmd.commandName == "edit") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "PYTHON_EDIT_NAME_MISSING",
                    "Missing script name for /edit.",
                    cmd.rawInput,
                    { "Usage: /edit <name>" }
                });
            }

            const auto script = LoadScript(state, "python", cmd.args[0]);
            if (!script.has_value()) {
                return std::unexpected(ExecutionError{ "PYTHON_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }

            auto editor = std::make_unique<Zeri::Engines::ScriptEditorContext>(
                m_executor,
                "python",
                cmd.args[0],
                SplitLines(*script)
            );
            state.PushContext(std::move(editor));

            auto* active = state.GetCurrentContext();
            if (active != nullptr) {
                active->OnEnter(terminal);
            }

            return "Python editor opened with script: " + cmd.args[0];
        }

        if (cmd.commandName == "list") {
            const auto scripts = ListScripts(state, "python");
            if (scripts.empty()) {
                return "No Python scripts saved.";
            }

            std::string output = "Python scripts:\n";
            for (const auto& script : scripts) {
                output += " - " + script + "\n";
            }
            return output;
        }

        if (cmd.commandName == "delete") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "PYTHON_DELETE_NAME_MISSING",
                    "Missing script name for /delete.",
                    cmd.rawInput,
                    { "Usage: /delete <name>" }
                });
            }

            if (!DeleteScript(state, "python", cmd.args[0])) {
                return std::unexpected(ExecutionError{ "PYTHON_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }
            return "Deleted Python script: " + cmd.args[0];
        }

        if (cmd.commandName == "show") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "PYTHON_SHOW_NAME_MISSING",
                    "Missing script name for /show.",
                    cmd.rawInput,
                    { "Usage: /show <name>" }
                });
            }

            const auto script = LoadScript(state, "python", cmd.args[0]);
            if (!script.has_value()) {
                return std::unexpected(ExecutionError{ "PYTHON_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }

            return *script;
        }

        return std::unexpected(ExecutionError{
            "PYTHON_UNKNOWN_COMMAND",
            "Unknown Python command: " + cmd.commandName,
            cmd.rawInput,
            { "Supported: /new, /run, /edit, /list, /delete, /show." }
        });
    }

}

/*
PythonContext.cpp
Implementa il contesto REPL Python con comandi /new, /run, /edit, /list, /delete,
/show e passthrough delle expression all'esecutore Python condiviso.
Usa ScriptRegistry su chiavi `python::scripts::*` e memorizza l'ultimo buffer in
sessione (`python::editor::last_buffer`) per supportare /run senza nome.
*/
