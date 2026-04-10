#include "../Include/LuaContext.h"

#include "../Include/LuaExecutor.h"
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

    LuaContext::LuaContext() {
        const auto health = Zeri::Core::SystemGuard::CheckEnvironment();
        if (const auto* runtime = health.GetRuntime("lua"); runtime != nullptr && runtime->available) {
            m_executor = std::make_shared<LuaExecutor>(*runtime);
        } else {
            Zeri::Core::ScriptRuntime missingRuntime;
            missingRuntime.language = "lua";
            m_executor = std::make_shared<LuaExecutor>(missingRuntime);
        }
    }

    void LuaContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        terminal.WriteInfo("Lua context active. Use /new <name>, /run, /list, /edit <name>, /show <name>, /delete <name>.");
    }

    ExecutionOutcome LuaContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string lastBufferKey = BuildLastBufferKey("lua");

        if (cmd.type == InputType::Expression) {
            Command execCmd;
            execCmd.rawInput = cmd.rawInput.empty() ? JoinArgs(cmd.args) : cmd.rawInput;
            state.SetSessionVariable(lastBufferKey, execCmd.rawInput);
            return m_executor->Execute(execCmd, state, terminal);
        }

        if (cmd.commandName == "new") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "LUA_NEW_NAME_MISSING",
                    "Missing script name for /new.",
                    cmd.rawInput,
                    { "Usage: /new <name>" }
                });
            }

            const std::string scriptName = cmd.args[0];
            if (const auto existing = LoadScript(state, "lua", scriptName); existing.has_value()) {
                return std::unexpected(ExecutionError{
                    "LUA_NEW_ALREADY_EXISTS",
                    "Script already exists: " + scriptName,
                    cmd.rawInput,
                    { "Use /edit <name> to modify it or choose a new script name." }
                });
            }

            auto editor = std::make_unique<Zeri::Engines::ScriptEditorContext>(m_executor, "lua", scriptName);
            state.PushContext(std::move(editor));

            auto* active = state.GetCurrentContext();
            if (active != nullptr) {
                active->OnEnter(terminal);
            }

            return "Lua editor opened for script: " + scriptName;
        }

        if (cmd.commandName == "run") {
            std::string code;
            if (!cmd.args.empty()) {
                const auto script = LoadScript(state, "lua", cmd.args[0]);
                if (!script.has_value()) {
                    return std::unexpected(ExecutionError{ "LUA_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
                }
                code = *script;
            } else {
                const auto lastBufferAny = state.GetSessionVariable(lastBufferKey);
                const auto lastBuffer = AnyToString(lastBufferAny);
                if (!lastBuffer.has_value() || lastBuffer->empty()) {
                    return std::unexpected(ExecutionError{
                        "LUA_LAST_BUFFER_MISSING",
                        "No last Lua buffer available.",
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
                    "LUA_EDIT_NAME_MISSING",
                    "Missing script name for /edit.",
                    cmd.rawInput,
                    { "Usage: /edit <name>" }
                });
            }

            const auto script = LoadScript(state, "lua", cmd.args[0]);
            if (!script.has_value()) {
                return std::unexpected(ExecutionError{ "LUA_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }

            auto editor = std::make_unique<Zeri::Engines::ScriptEditorContext>(
                m_executor,
                "lua",
                cmd.args[0],
                SplitLines(*script)
            );
            state.PushContext(std::move(editor));

            auto* active = state.GetCurrentContext();
            if (active != nullptr) {
                active->OnEnter(terminal);
            }

            return "Lua editor opened with script: " + cmd.args[0];
        }

        if (cmd.commandName == "list") {
            const auto scripts = ListScripts(state, "lua");
            if (scripts.empty()) {
                return "No Lua scripts saved.";
            }

            std::string output = "Lua scripts:\n";
            for (const auto& script : scripts) {
                output += " - " + script + "\n";
            }
            return output;
        }

        if (cmd.commandName == "delete") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "LUA_DELETE_NAME_MISSING",
                    "Missing script name for /delete.",
                    cmd.rawInput,
                    { "Usage: /delete <name>" }
                });
            }

            if (!DeleteScript(state, "lua", cmd.args[0])) {
                return std::unexpected(ExecutionError{ "LUA_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }
            return "Deleted Lua script: " + cmd.args[0];
        }

        if (cmd.commandName == "show") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "LUA_SHOW_NAME_MISSING",
                    "Missing script name for /show.",
                    cmd.rawInput,
                    { "Usage: /show <name>" }
                });
            }

            const auto script = LoadScript(state, "lua", cmd.args[0]);
            if (!script.has_value()) {
                return std::unexpected(ExecutionError{ "LUA_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }

            return *script;
        }

        return std::unexpected(ExecutionError{
            "LUA_UNKNOWN_COMMAND",
            "Unknown Lua command: " + cmd.commandName,
            cmd.rawInput,
            { "Supported: /new, /run, /edit, /list, /delete, /show." }
        });
    }

}

/*
LuaContext.cpp
Implementa il contesto REPL Lua language-specific con executor condiviso e comandi:
/new, /run, /edit, /list, /delete, /show, oltre al passthrough di espressioni raw.
Le operazioni CRUD usano ScriptRegistry su chiavi `lua::scripts::*`; /run senza nome
legge l'ultimo buffer dalla sessione RuntimeState (`lua::editor::last_buffer`).
/edit pre-carica ScriptEditorContext con contenuto dello script salvato.
*/
