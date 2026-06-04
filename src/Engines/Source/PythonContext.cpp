#include "../Include/PythonContext.h"

#include "../Include/ContextUtils.h"
#include "../Include/PythonExecutor.h"
#include "../Include/ScriptEditorContext.h"
#include "../Include/ScriptRegistry.h"
#include "../../Core/Include/SystemGuard.h"
#include "../../Core/Include/StringUtils.h"

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
        terminal.WriteInfo("Python context active. Use /new <name>, /run, /list, /edit <name>, /show <name>, /delete <name>.");
    }

    ExecutionOutcome PythonContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string lastBufferKey = Zeri::Engines::Utils::BuildLastBufferKey("python");

        if (cmd.type == InputType::Expression) {
            Command execCmd;
            execCmd.rawInput = cmd.rawInput.empty() ? Zeri::Core::Utils::JoinArgs(cmd.args) : cmd.rawInput;
            state.SetSessionVariable(lastBufferKey, execCmd.rawInput);
            return m_executor->Execute(execCmd, state, terminal);
        }

        if (cmd.commandName == "new") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "PYTHON_NEW_NAME_MISSING",
                    "Missing script name for /new.",
                    cmd.rawInput,
                    { "Usage: /new <name>" }
                });
            }

            const std::string scriptName = cmd.args[0];
            if (const auto existing = LoadScript(state, "python", scriptName); existing.has_value()) {
                return std::unexpected(ExecutionError{
                    "PYTHON_NEW_ALREADY_EXISTS",
                    "Script already exists: " + scriptName,
                    cmd.rawInput,
                    { "Use /edit <name> to modify it or choose a new script name." }
                });
            }

            auto editor = std::make_unique<Zeri::Engines::ScriptEditorContext>(m_executor, "python", scriptName);
            state.PushContext(std::move(editor));

            auto* active = state.GetCurrentContext();
            if (active != nullptr) {
                active->OnEnter(terminal);
            }

            return "Python editor opened for script: " + scriptName;
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
                const std::string lastBuffer = Zeri::Engines::Utils::AnyToString(lastBufferAny);
                if (lastBuffer.empty()) {
                    return std::unexpected(ExecutionError{
                        "PYTHON_LAST_BUFFER_MISSING",
                        "No last Python buffer available.",
                        cmd.rawInput,
                        { "Use /new and /run first, or execute /run <name>." }
                    });
                }
                code = lastBuffer;
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
                Zeri::Engines::Utils::SplitLines(*script)
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
Implements the Python REPL context with /new, /run, /edit, /list, /delete,
/show, and expression passthrough to the shared Python executor.
Uses ScriptRegistry keys `python::scripts::*` and stores the last buffer in
session state (`python::editor::last_buffer`) to support /run without a name.
*/
