#include "../Include/RubyContext.h"

#include "../Include/RubyExecutor.h"
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

    RubyContext::RubyContext() = default;

    void RubyContext::OnEnter(Zeri::Ui::ITerminal& terminal) {
        if (!m_initialized) {
            const auto health = Zeri::Core::SystemGuard::CheckEnvironment();
            if (const auto* runtime = health.GetRuntime("ruby"); runtime != nullptr && runtime->available) {
                m_executor = std::make_shared<RubyExecutor>(*runtime);
            } else {
                Zeri::Core::ScriptRuntime missingRuntime;
                missingRuntime.language = "ruby";
                m_executor = std::make_shared<RubyExecutor>(missingRuntime);
            }
            m_initialized = true;
        }
        terminal.WriteInfo("Ruby context active. Use /new <name>, /run, /list, /edit <name>, /show <name>, /delete <name>, /history <name>, /diff <name> <v1> <v2>, /rollback <name> <version>, /search <query>.");
    }

    bool RubyContext::RequestCancel() {
        if (!m_executor) {
            return false;
        }
        auto concrete = std::dynamic_pointer_cast<RubyExecutor>(m_executor);
        if (!concrete) {
            return false;
        }
        return concrete->CancelActiveExecution();
    }

    ExecutionOutcome RubyContext::HandleCommand(
        const Command& cmd,
        Zeri::Core::RuntimeState& state,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string lastBufferKey = BuildLastBufferKey("ruby");

        if (cmd.type == InputType::Expression) {
            Command execCmd;
            execCmd.rawInput = cmd.rawInput.empty() ? JoinArgs(cmd.args) : cmd.rawInput;
            state.SetSessionVariable(lastBufferKey, execCmd.rawInput);
            return m_executor->Execute(execCmd, state, terminal);
        }

        if (cmd.commandName == "new") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "RUBY_NEW_NAME_MISSING",
                    "Missing script name for /new.",
                    cmd.rawInput,
                    { "Usage: /new <name>" }
                });
            }

            const std::string scriptName = cmd.args[0];
            if (const auto existing = LoadScript(state, "ruby", scriptName); existing.has_value()) {
                return std::unexpected(ExecutionError{
                    "RUBY_NEW_ALREADY_EXISTS",
                    "Script already exists: " + scriptName,
                    cmd.rawInput,
                    { "Use /edit <name> to modify it or choose a new script name." }
                });
            }

            auto editor = std::make_unique<Zeri::Engines::ScriptEditorContext>(m_executor, "ruby", scriptName);
            state.PushContext(std::move(editor));

            auto* active = state.GetCurrentContext();
            if (active != nullptr) {
                active->OnEnter(terminal);
            }

            return "Ruby editor opened for script: " + scriptName;
        }

        if (cmd.commandName == "run") {
            std::string code;
            if (!cmd.args.empty()) {
                const auto script = LoadScript(state, "ruby", cmd.args[0]);
                if (!script.has_value()) {
                    return std::unexpected(ExecutionError{ "RUBY_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
                }
                code = *script;
            } else {
                const auto lastBufferAny = state.GetSessionVariable(lastBufferKey);
                const auto lastBuffer = AnyToString(lastBufferAny);
                if (!lastBuffer.has_value() || lastBuffer->empty()) {
                    return std::unexpected(ExecutionError{
                        "RUBY_LAST_BUFFER_MISSING",
                        "No last Ruby buffer available.",
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
                    "RUBY_EDIT_NAME_MISSING",
                    "Missing script name for /edit.",
                    cmd.rawInput,
                    { "Usage: /edit <name>" }
                });
            }

            const auto script = LoadScript(state, "ruby", cmd.args[0]);
            if (!script.has_value()) {
                return std::unexpected(ExecutionError{ "RUBY_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }

            auto editor = std::make_unique<Zeri::Engines::ScriptEditorContext>(
                m_executor,
                "ruby",
                cmd.args[0],
                SplitLines(*script)
            );
            state.PushContext(std::move(editor));

            auto* active = state.GetCurrentContext();
            if (active != nullptr) {
                active->OnEnter(terminal);
            }

            return "Ruby editor opened with script: " + cmd.args[0];
        }

        if (cmd.commandName == "list") {
            const auto scripts = ListScripts(state, "ruby");
            if (scripts.empty()) {
                return "No Ruby scripts saved.";
            }

            std::string output = "Ruby scripts:\n";
            for (const auto& script : scripts) {
                output += " - " + script + "\n";
            }
            return output;
        }

        if (cmd.commandName == "delete") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "RUBY_DELETE_NAME_MISSING",
                    "Missing script name for /delete.",
                    cmd.rawInput,
                    { "Usage: /delete <name> or /delete --hard <name> --confirm <name>" }
                });
            }

            if (cmd.args[0] == "--hard") {
                if (cmd.args.size() < 4 || cmd.args[2] != "--confirm" || cmd.args[1] != cmd.args[3]) {
                    return std::unexpected(ExecutionError{
                        "RUBY_DELETE_HARD_USAGE",
                        "Invalid hard delete syntax.",
                        cmd.rawInput,
                        { "Usage: /delete --hard <name> --confirm <name>" }
                    });
                }
                if (!HardDeleteScript(state, "ruby", cmd.args[1])) {
                    return std::unexpected(ExecutionError{ "RUBY_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[1], cmd.rawInput });
                }
                return "Hard-deleted Ruby script: " + cmd.args[1];
            }

            if (!DeleteScript(state, "ruby", cmd.args[0])) {
                return std::unexpected(ExecutionError{ "RUBY_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }
            return "Deleted Ruby script: " + cmd.args[0];
        }

        if (cmd.commandName == "show") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "RUBY_SHOW_NAME_MISSING",
                    "Missing script name for /show.",
                    cmd.rawInput,
                    { "Usage: /show <name>" }
                });
            }

            const auto script = LoadScript(state, "ruby", cmd.args[0]);
            if (!script.has_value()) {
                return std::unexpected(ExecutionError{ "RUBY_SCRIPT_NOT_FOUND", "Script not found: " + cmd.args[0], cmd.rawInput });
            }

            return *script;
        }

        if (cmd.commandName == "history") {
            if (cmd.args.empty()) {
                return std::unexpected(ExecutionError{
                    "RUBY_HISTORY_NAME_MISSING",
                    "Missing script name for /history.",
                    cmd.rawInput,
                    { "Usage: /history <name>" }
                });
            }

            const auto history = GetScriptHistory(state, "ruby", cmd.args[0]);
            if (history.empty()) {
                return std::unexpected(ExecutionError{ "RUBY_HISTORY_NOT_FOUND", "No version history found for script: " + cmd.args[0], cmd.rawInput });
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
                    "RUBY_DIFF_USAGE",
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
                    "RUBY_DIFF_VERSION_INVALID",
                    "Version arguments must be integers.",
                    cmd.rawInput,
                    { "Usage: /diff <name> <v1> <v2>" }
                });
            }

            const auto contentA = LoadScriptVersion(state, "ruby", cmd.args[0], versionA);
            const auto contentB = LoadScriptVersion(state, "ruby", cmd.args[0], versionB);
            if (!contentA.has_value() || !contentB.has_value()) {
                return std::unexpected(ExecutionError{
                    "RUBY_DIFF_VERSION_NOT_FOUND",
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
                    "RUBY_ROLLBACK_USAGE",
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
                    "RUBY_ROLLBACK_VERSION_INVALID",
                    "Version argument must be an integer.",
                    cmd.rawInput,
                    { "Usage: /rollback <name> <version>" }
                });
            }

            if (!RollbackScript(state, "ruby", cmd.args[0], sourceVersion)) {
                return std::unexpected(ExecutionError{
                    "RUBY_ROLLBACK_VERSION_NOT_FOUND",
                    "Version not found for rollback.",
                    cmd.rawInput
                });
            }
            return "Rollback completed for Ruby script: " + cmd.args[0];
        }

        if (cmd.commandName == "search") {
            const std::string query = JoinArgs(cmd.args);
            if (query.empty()) {
                return std::unexpected(ExecutionError{
                    "RUBY_SEARCH_QUERY_MISSING",
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
            "RUBY_UNKNOWN_COMMAND",
            "Unknown Ruby command: " + cmd.commandName,
            cmd.rawInput,
            { "Supported: /new, /run, /edit, /list, /delete, /show, /history, /diff, /rollback, /search." }
        });
    }

}

/*
RubyContext.cpp
Implements the Ruby REPL context with /new, /run, /edit, /list, /delete,
/show, and expression passthrough to the shared Ruby executor.
Uses ScriptRegistry keys `ruby::scripts::*` and stores the last buffer in
session state (`ruby::editor::last_buffer`) to support /run without a name.
*/
