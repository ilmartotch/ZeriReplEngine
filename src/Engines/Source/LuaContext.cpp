#include "../Include/LuaContext.h"

#include "../Include/ContextUtils.h"
#include "../Include/LuaExecutor.h"
#include "../Include/ScriptEditorContext.h"
#include "../Include/ScriptRegistry.h"
#include "../../Core/Include/SystemGuard.h"
#include "../../Core/Include/StringUtils.h"
#include <sstream>

namespace Zeri::Engines::Defaults {

    LuaContext::LuaContext() = default;

    void LuaContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        if (!m_initialized) {
            const auto health = Zeri::Core::SystemGuard::CheckEnvironment();
            if (const auto* runtime = health.GetRuntime("lua"); runtime != nullptr && runtime->available) {
                m_executor = std::make_shared<LuaExecutor>(*runtime);
            } else {
                Zeri::Core::ScriptRuntime missingRuntime;
                missingRuntime.language = "lua";
                m_executor = std::make_shared<LuaExecutor>(missingRuntime);
            }
            m_initialized = true;
        }
        terminal.WriteInfo("Lua context active. Use /new <name>, /run, /list, /edit <name>, /show <name>, /delete <name>, /history <name>, /diff <name> <v1> <v2>, /rollback <name> <version>, /search <query>.");
    }

    bool LuaContext::RequestCancel() {
        if (!m_executor) {
            return false;
        }
        auto concrete = std::dynamic_pointer_cast<LuaExecutor>(m_executor);
        if (!concrete) {
            return false;
        }
        return concrete->CancelActiveExecution();
    }

    ExecutionOutcome LuaContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string lastBufferKey = Zeri::Engines::Utils::BuildLastBufferKey("lua");

        if (cmd.type == InputType::Expression) {
            Command execCmd;
            execCmd.rawInput = cmd.rawInput.empty() ? Zeri::Core::Utils::JoinArgs(cmd.args) : cmd.rawInput;
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
                const std::string lastBuffer = Zeri::Engines::Utils::AnyToString(lastBufferAny);
                if (lastBuffer.empty()) {
                    return std::unexpected(ExecutionError{
                        "LUA_LAST_BUFFER_MISSING",
                        "No last Lua buffer available.",
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
                Zeri::Engines::Utils::SplitLines(*script)
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
                    { "Usage: /delete <name> or /delete --hard <name> --confirm <name>" }
                });
            }

            if (cmd.args[0] == "--hard") {
                if (cmd.args.size() < 4 || cmd.args[2] != "--confirm" || cmd.args[1] != cmd.args[3]) {
                    return std::unexpected(ExecutionError{
                        "LUA_DELETE_HARD_USAGE",
                        "Invalid hard delete syntax.",
                        cmd.rawInput,
                        { "Usage: /delete --hard <name> --confirm <name>" }
                    });
                }
                if (!HardDeleteScript(state, "lua", cmd.args[1])) {
                    return std::unexpected(ExecutionError{ "LUA_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[1], cmd.rawInput });
                }
                return "Hard-deleted Lua script: " + cmd.args[1];
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

        if (cmd.commandName == "history") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "LUA_HISTORY_NAME_MISSING",
                    "Missing script name for /history.",
                    cmd.rawInput,
                    { "Usage: /history <name>" }
                });
            }

            const auto history = GetScriptHistory(state, "lua", cmd.args[0]);
            if (history.empty()) {
                return std::unexpected(ExecutionError{ "LUA_HISTORY_NOT_FOUND", "No version history found for script: " + cmd.args[0], cmd.rawInput });
            }

            std::ostringstream output;
            output << cmd.args[0] << " - " << history.size() << " versions\n";
            for (const auto& item : history) {
                output << "  v" << item.version << "  " << item.timestamp << "  " << item.sizeBytes << " bytes";
                if (item.current) {
                    output << "  <- current";
                }
                output << "\n";
            }
            return output.str();
        }

        if (cmd.commandName == "diff") {
            if (cmd.args.size() < 3) {
                return std::unexpected(ExecutionError{
                    "LUA_DIFF_USAGE",
                    "Missing arguments for /diff.",
                    cmd.rawInput,
                    { "Usage: /diff <name> <v1> <v2>" }
                });
            }

            int versionA = 0;
            int versionB = 0;
            try {
                versionA = std::stoi(cmd.args[1]);
                versionB = std::stoi(cmd.args[2]);
            } catch (const std::exception&) {
                return std::unexpected(ExecutionError{
                    "LUA_DIFF_VERSION_INVALID",
                    "Version arguments must be integers.",
                    cmd.rawInput,
                    { "Usage: /diff <name> <v1> <v2>" }
                });
            }

            const auto contentA = LoadScriptVersion(state, "lua", cmd.args[0], versionA);
            const auto contentB = LoadScriptVersion(state, "lua", cmd.args[0], versionB);
            if (!contentA.has_value() || !contentB.has_value()) {
                return std::unexpected(ExecutionError{
                    "LUA_DIFF_VERSION_NOT_FOUND",
                    "One or both versions were not found.",
                    cmd.rawInput
                });
            }

            const auto diffLines = BuildUnifiedDiff(cmd.args[0], versionA, versionB, *contentA, *contentB);
            for (const auto& line : diffLines) {
                if (line.kind == ScriptDiffLine::Kind::Removal) {
                    terminal.WriteError(line.text);
                    continue;
                }
                if (line.kind == ScriptDiffLine::Kind::Addition) {
                    terminal.WriteSuccess(line.text);
                    continue;
                }
                terminal.WriteInfo(line.text);
            }
            return "";
        }

        if (cmd.commandName == "rollback") {
            if (cmd.args.size() < 2) {
                return std::unexpected(ExecutionError{
                    "LUA_ROLLBACK_USAGE",
                    "Missing arguments for /rollback.",
                    cmd.rawInput,
                    { "Usage: /rollback <name> <version>" }
                });
            }

            int sourceVersion = 0;
            try {
                sourceVersion = std::stoi(cmd.args[1]);
            } catch (const std::exception&) {
                return std::unexpected(ExecutionError{
                    "LUA_ROLLBACK_VERSION_INVALID",
                    "Version argument must be an integer.",
                    cmd.rawInput,
                    { "Usage: /rollback <name> <version>" }
                });
            }

            if (!RollbackScript(state, "lua", cmd.args[0], sourceVersion)) {
                return std::unexpected(ExecutionError{
                    "LUA_ROLLBACK_VERSION_NOT_FOUND",
                    "Version not found for rollback.",
                    cmd.rawInput
                });
            }
            return "Rollback completed for Lua script: " + cmd.args[0];
        }

        if (cmd.commandName == "search") {
            const std::string query = Zeri::Core::Utils::JoinArgs(cmd.args);
            if (query.empty()) {
                return std::unexpected(ExecutionError{
                    "LUA_SEARCH_QUERY_MISSING",
                    "Missing query for /search.",
                    cmd.rawInput,
                    { "Usage: /search <query>" }
                });
            }

            const auto matches = SearchScripts(state, query);
            if (matches.empty()) {
                return "No scripts matched query: " + query;
            }

            std::ostringstream output;
            output << "Search results for \"" << query << "\"\n";
            for (const auto& match : matches) {
                output << " - [" << match.language << "] " << match.name
                    << "  " << match.sizeBytes << " bytes"
                    << "  " << match.modifiedUtc << "\n";
            }
            return output.str();
        }

        return std::unexpected(ExecutionError{
            "LUA_UNKNOWN_COMMAND",
            "Unknown Lua command: " + cmd.commandName,
            cmd.rawInput,
            { "Supported: /new, /run, /edit, /list, /delete, /show, /history, /diff, /rollback, /search." }
        });
    }

}

/*
LuaContext.cpp
Implements the language-specific Lua REPL context with shared executor and commands:
/new, /run, /edit, /list, /delete, /show, plus passthrough of raw expressions.
CRUD operations use ScriptRegistry keys `lua::scripts::*`; /run without a name
reads the last buffer from RuntimeState session state (`lua::editor::last_buffer`).
/edit preloads ScriptEditorContext with saved script content.
*/
