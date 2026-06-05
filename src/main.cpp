#include "Core/Include/RuntimeState.h"
#include "Core/Include/HelpCatalog.h"
#include "Core/Include/SystemGuard.h"
#include "Core/Include/StartupDiagnostics.h"
#include "Core/Include/UserPaths.h"
#include "Core/Include/AppPaths.h"
#include "Core/Include/BugSnapshot.h"
#include "Engines/Include/GlobalContext.h"
#include "Engines/Include/CustomCommandContext.h"
#include "Engines/Include/JsContext.h"
#include "Engines/Include/LuaContext.h"
#include "Engines/Include/MathContext.h"
#include "Engines/Include/PythonContext.h"
#include "Engines/Include/ScriptEditorContext.h"
#include "Engines/Include/ScriptHubContext.h"
#include "Engines/Include/RubyContext.h"
#include "Engines/Include/SandboxContext.h"
#include "Engines/Include/SetupContext.h"
#include "Ui/Include/BridgeTerminal.h"
#include "Ui/Include/BridgeProtocol.h"
#include "Ui/Include/OutputSink.h"
#include "Engines/Include/MetaParser.h"
#include "Engines/Include/DefaultDispatcher.h"
#include "Engines/Include/CachedDispatcher.h"
#include "Engines/Include/GlobalCommandRegistry.h"

#include <yuumi/bridge.hpp>

#include <memory>
#include <format>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <vector>
#include <optional>
#include <exception>
#include <sstream>
#include <filesystem>

#ifndef ZERI_ENGINE_VERSION
#define ZERI_ENGINE_VERSION "unknown"
#endif

namespace Zeri::Platform {
#ifdef _WIN32
    inline FILE* POpen(const char* cmd, const char* mode) {
        return _popen(cmd, mode);
    }

    inline int PClose(FILE* pipe) {
        return _pclose(pipe);
    }
#else
    inline FILE* POpen(const char* cmd, const char* mode) {
        return popen(cmd, mode);
    }

    inline int PClose(FILE* pipe) {
        return pclose(pipe);
    }
#endif
}

namespace {
    [[nodiscard]] bool IsUnreservedUrlChar(unsigned char c) {
        return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.' || c == '~';
    }

    [[nodiscard]] std::string UrlEncode(std::string_view input) {
        static constexpr char kHex[] = "0123456789ABCDEF";
        std::string encoded;
        encoded.reserve(input.size() * 3U);

        for (unsigned char c : input) {
            if (IsUnreservedUrlChar(c)) {
                encoded.push_back(static_cast<char>(c));
            } else {
                encoded.push_back('%');
                encoded.push_back(kHex[(c >> 4U) & 0x0FU]);
                encoded.push_back(kHex[c & 0x0FU]);
            }
        }

        return encoded;
    }

    [[nodiscard]] std::string BuildIssueReportBody(
        const Zeri::Core::RuntimeState& runtimeState,
        const Zeri::Core::StartupDiagnosticsReport* startupDiagnostics
    ) {
        const auto* ctx = runtimeState.GetCurrentContext();
        const std::string contextName = ctx ? ctx->GetName() : "global";
        const auto localVars = runtimeState.GetCurrentLocalVariables();
        const auto localFuncs = runtimeState.GetCurrentLocalFunctions();
        const auto& helpCatalog = Zeri::Core::HelpCatalog::Instance();

        std::string body;
        body += "## Bug description\n";
        body += "Describe what happened and how to reproduce it.\n\n";
        body += "## Diagnostics snapshot\n";
        body += "- Context: " + contextName + "\n";
        body += "- Local variables: " + std::to_string(localVars.size()) + "\n";
        body += "- Local functions: " + std::to_string(localFuncs.size()) + "\n";
        body += "- Function registry revision: " + std::to_string(runtimeState.GetFunctionRegistryRevision()) + "\n";
        body += "- Session was corrupted: " + std::string(runtimeState.WasSessionCorrupted() ? "yes" : "no") + "\n";
        body += "- Help catalog loaded: " + std::string(helpCatalog.IsLoaded() ? "yes" : "no") + "\n";
        body += "- Help catalog source: `" + helpCatalog.SourcePath().string() + "`\n";

        if (helpCatalog.LastError().empty()) {
            body += "- Help catalog error: none\n";
        } else {
            body += "- Help catalog error: " + helpCatalog.LastError() + "\n";
        }

        if (startupDiagnostics != nullptr) {
            body += "- Executable directory: `" + startupDiagnostics->executableDir.string() + "`\n";
            body += "- Startup issue count: " + std::to_string(startupDiagnostics->issues.size()) + "\n";
            for (const auto& issue : startupDiagnostics->issues) {
                body += "  - " + issue.code + ": " + issue.message + " (Hint: " + issue.hint + ")\n";
            }
        }

        body += "\n## Logs\n";
        body += "Attach generated bug snapshot and steps already attempted.\n";
        return body;
    }

    [[nodiscard]] std::string BuildPrefilledIssueUrl(
        const Zeri::Core::RuntimeState& runtimeState,
        const Zeri::Core::StartupDiagnosticsReport* startupDiagnostics
    ) {
        const std::string baseUrl = "https://github.com/ilmartotch/ReplZeriEmgine/issues/new";
        const std::string title = "Bug report: describe the failure here";
        const std::string body = BuildIssueReportBody(runtimeState, startupDiagnostics);
        return baseUrl + "?title=" + UrlEncode(title) + "&body=" + UrlEncode(body);
    }

    [[nodiscard]] bool CanReachContext(std::string_view from, std::string_view target) {
        const auto reachable = Zeri::Core::HelpCatalog::Instance().ReachableFrom(from);
        return std::ranges::find(reachable, std::string(target)) != reachable.end();
    }

    [[nodiscard]] std::string_view Trim(std::string_view value) {
        auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
        while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
        return value;
    }

    [[nodiscard]] std::string ToLower(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return result;
    }

    [[nodiscard]] std::string CommandKindFromInput(std::string_view input) {
        const std::string trimmed = ToLower(std::string(Trim(input)));
        if (trimmed.empty()) return "empty";
        if (trimmed.starts_with("$")) return "context_switch";
        if (trimmed.starts_with("/")) return "slash_command";
        return "expression";
    }

    [[nodiscard]] std::string CommandPreviewFromInput(std::string_view input) {
        std::string trimmed = std::string(Trim(input));
        if (trimmed.empty()) {
            return "<empty>";
        }
        if (!trimmed.starts_with('/') && !trimmed.starts_with('$')) {
            return "<expression>";
        }
        static constexpr std::size_t kMaxPreviewLength = 160;
        if (trimmed.size() > kMaxPreviewLength) {
            trimmed.resize(kMaxPreviewLength);
            trimmed += "...";
        }
        return trimmed;
    }

    void AppendCommandHistory(
        std::vector<Zeri::Core::BugSnapshotCommandRecord>& history,
        std::string_view input,
        bool success,
        std::string responseCode
    ) {
        static constexpr std::size_t kMaxHistory = 50;
        Zeri::Core::BugSnapshotCommandRecord record;
        record.command = CommandPreviewFromInput(input);
        record.kind = CommandKindFromInput(input);
        record.success = success;
        record.responseCode = responseCode.empty() ? (success ? "OK" : "FAILED") : std::move(responseCode);
        history.push_back(std::move(record));
        while (history.size() > kMaxHistory) {
            history.erase(history.begin());
        }
    }

    struct PersistencePathsSnapshot {
        std::filesystem::path userDataDir;
        std::filesystem::path scriptsDir;
        std::filesystem::path sessionsDir;
        std::filesystem::path stateFilePath;
    };

    [[nodiscard]] std::optional<PersistencePathsSnapshot> TryCollectPersistencePaths() {
        const auto userDataDir = Zeri::Core::TryResolveUserDataDir();
        const auto scriptsDir = Zeri::Core::TryResolveScriptsDir();
        const auto sessionsDir = Zeri::Core::TryResolveSessionsDir();

        if (!userDataDir.has_value() || !scriptsDir.has_value() || !sessionsDir.has_value()) {
            return std::nullopt;
        }

        PersistencePathsSnapshot snapshot;
        snapshot.userDataDir = *userDataDir;
        snapshot.scriptsDir = *scriptsDir;
        snapshot.sessionsDir = *sessionsDir;
        snapshot.stateFilePath = snapshot.sessionsDir / "state.json";
        return snapshot;
    }

    [[nodiscard]] std::string BuildPersistencePathsBlock(
        const PersistencePathsSnapshot& paths,
        std::string_view indent
    ) {
        std::string output;
        output += std::string(indent) + "Data root: " + paths.userDataDir.string() + "\n";
        output += std::string(indent) + "Scripts: " + (paths.scriptsDir / "<language>").string() + "\n";
        output += std::string(indent) + "Sessions: " + paths.sessionsDir.string() + "\n";
        output += std::string(indent) + "Session state: " + paths.stateFilePath.string();
        return output;
    }

    [[nodiscard]] std::unique_ptr<Zeri::Engines::IContext> BuildContext(const std::string& name) {
        const std::string normalized = ToLower(name);
        if (normalized == "code") return std::make_unique<Zeri::Engines::Defaults::ScriptHubContext>();
        if (normalized == "customcommand") return std::make_unique<Zeri::Engines::Defaults::CustomCommandContext>();
        if (normalized == "js") return std::make_unique<Zeri::Engines::Defaults::JsContext>(false);
        if (normalized == "ts") return std::make_unique<Zeri::Engines::Defaults::JsContext>(true);
        if (normalized == "lua") return std::make_unique<Zeri::Engines::Defaults::LuaContext>();
        if (normalized == "python") return std::make_unique<Zeri::Engines::Defaults::PythonContext>();
        if (normalized == "ruby") return std::make_unique<Zeri::Engines::Defaults::RubyContext>();
        if (normalized == "math") return std::make_unique<Zeri::Engines::Defaults::MathContext>();
        if (normalized == "sandbox") return std::make_unique<Zeri::Engines::Defaults::SandboxContext>();
        if (normalized == "setup") return std::make_unique<Zeri::Engines::Defaults::SetupContext>();
        return nullptr;
    }

    [[nodiscard]] std::string EnsureStandardErrorMessage(
        std::string text,
        std::string_view fallbackCode,
        std::string_view fallbackHint
    ) {
        if (text.rfind("[ZERI][", 0) != 0) {
            text = std::string("[ZERI][") + std::string(fallbackCode) + "] " + text;
        }
        if (text.find("Hint:") == std::string::npos) {
            text += " Hint: ";
            text += fallbackHint;
        }
        return text;
    }

    void HandleOutcome(const Zeri::Engines::ExecutionOutcome& outcome, Zeri::Ui::ITerminal& terminal) {
        if (outcome.has_value()) {
            const auto& message = outcome.value();
            const auto& text = message.text;

            if (text.empty()) {
                terminal.WriteInfo("(ok)");
            } else {
                switch (message.kind) {
                case Zeri::Engines::ExecutionMessageKind::Info:
                    terminal.WriteInfo(text);
                    break;
                case Zeri::Engines::ExecutionMessageKind::Success:
                    terminal.WriteSuccess(text);
                    break;
                case Zeri::Engines::ExecutionMessageKind::Warning:
                    terminal.WriteError(EnsureStandardErrorMessage(
                        text,
                        "CONTEXT-006",
                        "run /help in the current context and retry the command."
                    ));
                    break;
                case Zeri::Engines::ExecutionMessageKind::Output:
                default:
                    terminal.WriteLine(text);
                    break;
                }
            }
        } else {
            const auto& err = outcome.error();
            std::string formatted = err.Format();
            terminal.WriteError(EnsureStandardErrorMessage(
                std::move(formatted),
                "CONTEXT-007",
                "review the error code and retry with the documented command syntax."
            ));
        }
    }

    void EmitBatchEnd(Zeri::Ui::OutputSink* sink, std::string_view reason) {
        if (sink == nullptr) {
            return;
        }
        nlohmann::json message;
        message["type"] = Zeri::Ui::kBridgeTypeStreamBatchEnd;
        message["reason"] = reason;
        sink->Send(message);
    }

    void EmitContextChanged(
        Zeri::Ui::OutputSink* sink,
        const std::string& context,
        const std::string& prompt
    ) {
        if (sink == nullptr) {
            return;
        }
        nlohmann::json message;
        message["type"] = "context_changed";
        message["context"] = context;
        message["prompt"] = prompt;
        sink->Send(message);
        EmitBatchEnd(sink, Zeri::Ui::kBatchEndContextTransition);
    }

    [[nodiscard]] bool SwitchContext(
        const std::string& ctxName,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal,
        Zeri::Ui::OutputSink* sink = nullptr
    ) {
        const std::string normalized = ToLower(ctxName);

        if (normalized == "global") {
            auto* current = runtimeState.GetCurrentContext();
            while (current && current->GetName() != "global") {
                current->OnExit(terminal);
                runtimeState.PopContext();
                current = runtimeState.GetCurrentContext();
            }

            auto* activeContext = runtimeState.GetCurrentContext();
            if (activeContext != nullptr) {
                activeContext->OnEnter(terminal);
                EmitContextChanged(sink, activeContext->GetName(), activeContext->GetPrompt());
                return true;
            }

            terminal.WriteError("[ZERI][CONTEXT-001] No active context is available. Hint: run /reset to restore the global context.");
            return false;
        }

        const auto* current = runtimeState.GetCurrentContext();
        const std::string currentName = current ? ToLower(current->GetName()) : std::string{ "global" };
        if (!CanReachContext(currentName, normalized)) {
            terminal.WriteError(
                "[ZERI][CONTEXT-002] Context switch is not allowed from $" + currentName + " to $" + normalized +
                ". Hint: run /context to list reachable targets."
            );
            return false;
        }

        auto nextContext = BuildContext(normalized);
        if (!nextContext) {
            terminal.WriteError("[ZERI][CONTEXT-003] Unknown context: " + ctxName + ". Hint: run /context and use one listed target.");
            return false;
        }

        runtimeState.PushContext(std::move(nextContext));
        auto* pushed = runtimeState.GetCurrentContext();
        pushed->OnEnter(terminal);
        EmitContextChanged(sink, pushed->GetName(), pushed->GetPrompt());
        return true;
    }

    [[nodiscard]] bool HandleGlobalCommand(
        const Zeri::Engines::Command& cmd,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal,
        Zeri::Ui::OutputSink* sink = nullptr,
        const Zeri::Core::StartupDiagnosticsReport* startupDiagnostics = nullptr,
        const std::vector<Zeri::Core::BugSnapshotCommandRecord>* commandHistory = nullptr
    ) {
        if (!Zeri::Engines::IsGlobalCommand(cmd.commandName)) {
            return false;
        }

        if (cmd.commandName == "context") {
            const auto* current = runtimeState.GetCurrentContext();
            const std::string currentName = ToLower(current ? current->GetName() : "global");
            const auto reachable = Zeri::Core::HelpCatalog::Instance().ReachableFrom(currentName);

            std::string result = "Reachable contexts from $" + currentName + ":\n";
            for (const auto& contextName : reachable) {
                const auto* context = Zeri::Core::HelpCatalog::Instance().FindContext(contextName);
                if (context == nullptr) {
                    continue;
                }
                result += "  $";
                result += context->name;
                result += " — ";
                result += context->description;
                if (ToLower(context->name) == currentName) {
                    result += " [active]";
                }
                result += '\n';
            }

            result += "\nUse $<context> to switch to a reachable context.";
            terminal.WriteLine(result);
            return true;
        }

        if (cmd.commandName == "back") {
            auto* current = runtimeState.GetCurrentContext();
            if (!current || current->GetName() == "global") {
                terminal.WriteInfo("Already at root context.");
                return true;
            }

            current->OnExit(terminal);
            runtimeState.PopContext();

            auto* parent = runtimeState.GetCurrentContext();
            if (parent) {
                terminal.WriteSuccess("Returned to " + parent->GetName() + " context.");
                parent->OnEnter(terminal);
                EmitContextChanged(sink, parent->GetName(), parent->GetPrompt());
            }
            return true;
        }

        if (cmd.commandName == "status") {
            const auto* ctx = runtimeState.GetCurrentContext();
            const std::string ctxName = ctx ? ctx->GetName() : "global";

            auto localVars = runtimeState.GetCurrentLocalVariables();
            auto localFuncs = runtimeState.GetCurrentLocalFunctions();

            std::string result = "Session Status\n";
            result += "  Context:    " + ctxName + "\n";
            result += "  Local vars: " + std::to_string(localVars.size()) + "\n";
            result += "  Local fns:  " + std::to_string(localFuncs.size()) + "\n";
            result += "  Fn rev:     " + std::to_string(runtimeState.GetFunctionRegistryRevision()) + "\n";
            result += "  Persistence paths:\n";
            if (const auto persistencePaths = TryCollectPersistencePaths(); persistencePaths.has_value()) {
                result += BuildPersistencePathsBlock(*persistencePaths, "    ");
            } else {
                result += "    Unavailable: unable to resolve user data directory from environment.";
            }
            terminal.WriteLine(result);
            return true;
        }

        if (cmd.commandName == "reset") {
            runtimeState.ResetSession();

            auto* ctx = runtimeState.GetCurrentContext();
            const std::string ctxName = ctx ? ctx->GetName() : "global";

            terminal.WriteSuccess("Session reset. Context: " + ctxName);

            EmitContextChanged(sink, ctxName, ctx ? ctx->GetPrompt() : "zeri");
            return true;
        }

        if (cmd.commandName == "exit") {
            runtimeState.RequestExit();
            terminal.WriteInfo("Exiting...");
            return true;
        }

        if (cmd.commandName == "save") {
            auto sessionPath = Zeri::Core::ResolveSessionPath();
            auto result = runtimeState.SaveSession(sessionPath);
            if (result.has_value()) {
                terminal.WriteSuccess("Session saved successfully.");
            } else {
                terminal.WriteError("[ZERI][SESSION-001] Failed to save session: " + result.error() + ". Hint: verify write permission for the sessions directory shown by /status.");
            }
            return true;
        }

        if (cmd.commandName == "bug") {
            if (cmd.args.empty() || (cmd.args.size() == 1 && ToLower(cmd.args[0]) == "report")) {
                const std::string trackerUrl = "https://github.com/ilmartotch/ReplZeriEmgine/issues";
                const std::string prefilledUrl = BuildPrefilledIssueUrl(runtimeState, startupDiagnostics);

                std::string message = "Bug Report Guide\n";
                message += "1. Open issue tracker: " + trackerUrl + "\n";
                message += "2. Optional prefilled issue draft: " + prefilledUrl + "\n";
                message += "3. Create project snapshot: /bug snapshot\n";
                message += "4. Attach generated snapshot file to the GitHub issue\n";
                message += "5. Add exact reproduction steps and expected vs actual behavior.\n";
                terminal.WriteLine(message);
                return true;
            }

            if (cmd.args.size() == 1 && ToLower(cmd.args[0]) == "snapshot") {
                const auto selection = terminal.SelectMenu(
                    "Create bug snapshot now?",
                    {"Yes - create snapshot", "No - cancel"}
                );
                if (!selection.has_value() || selection.value() != 0) {
                    terminal.WriteInfo("Bug snapshot generation canceled.");
                    return true;
                }

                terminal.WriteInfo("Creating bug snapshot, please wait...");

                std::error_code ec;
                auto projectRoot = std::filesystem::current_path(ec);
                if (ec) {
                    projectRoot = startupDiagnostics ? startupDiagnostics->executableDir : Zeri::Core::ResolveExecutableDir();
                }

                const auto diagnostics = startupDiagnostics ? *startupDiagnostics : Zeri::Core::CollectStartupDiagnostics();
                Zeri::Core::BugSnapshotMetadata metadata;
                metadata.triggerCommand = "/bug snapshot";
                if (commandHistory != nullptr) {
                    metadata.commandHistory = *commandHistory;
                }
                metadata.commandHistory.push_back({
                    "/bug snapshot",
                    "slash_command",
                    "REQUESTED",
                    true
                });
                const auto snapshotResult = Zeri::Core::CreateBugSnapshot(runtimeState, diagnostics, projectRoot, metadata);
                if (!snapshotResult.has_value()) {
                    terminal.WriteError("[ZERI][SESSION-002] Failed to generate bug snapshot: " + snapshotResult.error() + ". Hint: verify file permissions in the project directory and retry /bug snapshot.");
                    return true;
                }

                terminal.WriteSuccess("Bug snapshot generated successfully.");
                terminal.WriteLine("Snapshot file: " + snapshotResult->string());
                terminal.WriteLine("Attach this file to your GitHub issue.");
                return true;
            }

            terminal.WriteInfo("Usage: /bug report | /bug snapshot");
            return true;
        }

        return false;
    }

    [[nodiscard]] bool ExecuteSystemOp(
        const Zeri::Engines::Command& cmd,
        Zeri::Ui::ITerminal& terminal
    ) {
        std::string shellCmd;
        shellCmd += cmd.commandName;
        for (const auto& arg : cmd.args) {
            shellCmd += ' ';
            shellCmd += arg;
        }

        if (shellCmd.empty()) {
            terminal.WriteError("[ZERI][PARSE-001] Empty system command. Hint: use !<command>, for example !echo hello.");
            return false;
        }

        std::string fullCmd = shellCmd + " 2>&1";

        std::array<char, 256> buffer{};
        std::string output;

        FILE* pipe = Zeri::Platform::POpen(fullCmd.c_str(), "r");

        if (!pipe) {
            terminal.WriteError("[ZERI][RUNTIME-001] Failed to execute system command: " + shellCmd + ". Hint: confirm the command exists in PATH and retry.");
            return false;
        }

        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

        int exitCode = Zeri::Platform::PClose(pipe);

        if (!output.empty()) {
            if (output.back() == '\n') output.pop_back();
            terminal.WriteLine(output);
        } else if (exitCode == 0) {
            terminal.WriteInfo("(command completed with no output)");
        }

        if (exitCode != 0) {
            terminal.WriteError("[ZERI][RUNTIME-002] System command exited with code: " + std::to_string(exitCode) + ". Hint: check command output above and fix the failing command.");
            return false;
        }

        return true;
    }

    [[nodiscard]] bool ExecuteStage(
        const std::string& stageInput,
        Zeri::Engines::Defaults::CachedDispatcher& dispatcher,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal,
        Zeri::Ui::OutputSink* sink = nullptr,
        const Zeri::Core::StartupDiagnosticsReport* startupDiagnostics = nullptr,
        const std::vector<Zeri::Core::BugSnapshotCommandRecord>* commandHistory = nullptr
    ) {
        auto dispatchResult = dispatcher.Dispatch(stageInput);
        if (!dispatchResult.has_value()) {
            const auto& err = dispatchResult.error();
            std::string caret(err.position, ' ');
            caret += '^';
            terminal.WriteError("[ZERI][PARSE-002] " + err.message + ". Hint: fix command syntax and retry.\n  " + stageInput + "\n  " + caret);
            return false;
        }

        auto cmd = dispatchResult->command;
        if (cmd.empty()) {
            terminal.WriteInfo("(empty input — no operation)");
            return true;
        }

        const auto hasPipeOperator = [&cmd]() -> bool {
            if (cmd.rawInput.find('|') != std::string::npos) {
                return true;
            }
            if (cmd.commandName.find('|') != std::string::npos) {
                return true;
            }
            for (const auto& arg : cmd.args) {
                if (arg.find('|') != std::string::npos) {
                    return true;
                }
            }
            return false;
        }();

        if (hasPipeOperator) {
            terminal.WriteError("[ZERI][PARSE-003] Unknown command. Hint: run /help to see available commands.");
            return false;
        }

        switch (cmd.type) {
        case Zeri::Engines::InputType::ContextSwitch:
            if (!cmd.args.empty() || !cmd.flags.empty()) {
                terminal.WriteError("[ZERI][PARSE-004] Invalid context switch syntax. Hint: use $<context> without flags or extra arguments.");
                return false;
            }
            return SwitchContext(cmd.commandName, runtimeState, terminal, sink);

        case Zeri::Engines::InputType::SystemOp:
            return ExecuteSystemOp(cmd, terminal);

        case Zeri::Engines::InputType::Expression:
        case Zeri::Engines::InputType::Command: {
            auto* currentCtx = runtimeState.GetCurrentContext();
            if (!currentCtx) {
                terminal.WriteError("[ZERI][CONTEXT-004] No active context is available. Hint: run /reset to restore the global context.");
                return false;
            }

            if (cmd.type == Zeri::Engines::InputType::Command &&
                currentCtx->IsGlobalCommand(cmd.commandName)) {
                if (HandleGlobalCommand(cmd, runtimeState, terminal, sink, startupDiagnostics, commandHistory)) {
                    return true;
                }
            }

            auto outcome = currentCtx->HandleCommand(cmd, runtimeState, terminal);
            HandleOutcome(outcome, terminal);

            if (!outcome.has_value()) return false;
            return true;
        }

        default:
            terminal.WriteError("[ZERI][PARSE-005] Unrecognized input type. Hint: run /help to review supported input forms.");
            return false;
        }
    }

}

namespace {
    [[nodiscard]] bool IsVersionArg(int argc, char* argv[]) {
        if (argc < 2) {
            return false;
        }
        const std::string_view arg(argv[1]);
        return arg == "--version" || arg == "-v";
    }

    [[nodiscard]] std::optional<std::string> ParsePipeArg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            if (std::string_view(argv[i]) == "--yuumi-pipe") {
                return std::string(argv[i + 1]);
            }
        }
        return std::nullopt;
    }

    void LogStartupLine(const std::string& text) {
        std::fprintf(stderr, "[ZERI_ENGINE][STARTUP] %s\n", text.c_str());
        std::fflush(stderr);
    }

    void EmitStartupDiagnostics(
        const Zeri::Core::StartupDiagnosticsReport& diagnostics,
        Zeri::Ui::ITerminal* terminal
    ) {
        const std::string base = "Executable directory: " + diagnostics.executableDir.string();
        LogStartupLine(base);
        if (terminal != nullptr && !diagnostics.issues.empty()) {
            terminal->WriteInfo(base);
        }

        for (const auto& issue : diagnostics.issues) {
            const std::string line = issue.code + " - " + issue.message + " Hint: " + issue.hint;
            LogStartupLine(line);
            if (terminal != nullptr) {
                terminal->WriteError("[ZERI][RUNTIME-003] Startup diagnostic: " + line + " Hint: fix the reported runtime or startup dependency.");
            }
        }
    }
}

int RunMain(int argc, char* argv[]) {
    if (IsVersionArg(argc, argv)) {
        std::printf("zeri-engine version %s\n", ZERI_ENGINE_VERSION);
        return 0;
    }

    auto pipeArg = ParsePipeArg(argc, argv);
    if (!pipeArg.has_value()) {
        std::fprintf(stderr, "[ZERI_ENGINE] Missing required --yuumi-pipe <name> argument. Local C++ UI is disabled.\n");
        std::fflush(stderr);
        return 1;
    }

    std::unique_ptr<Zeri::Ui::ITerminal> terminalOwner;
    std::unique_ptr<yuumi::Bridge> bridgeOwner;
    std::unique_ptr<Zeri::Ui::OutputSink> sinkOwner;
    Zeri::Ui::BridgeTerminal* bridgeTerminal = nullptr;
    const auto startupDiagnostics = Zeri::Core::CollectStartupDiagnostics();
    EmitStartupDiagnostics(startupDiagnostics, nullptr);

    bridgeOwner = std::make_unique<yuumi::Bridge>();
    auto& bridge = *bridgeOwner;

    sinkOwner = std::make_unique<Zeri::Ui::OutputSink>(bridge);

    auto bt = std::make_unique<Zeri::Ui::BridgeTerminal>(*sinkOwner);
    bridgeTerminal = bt.get();
    terminalOwner = std::move(bt);

    bridge.on_message([bridgeTerminal](const nlohmann::json& msg, yuumi::Channel) {
        std::string type = msg.value("type", "");

        if (type == "command") {
            std::string payload = msg.value("payload", "");
            bridgeTerminal->EnqueueCommand(payload);
        } else if (type == "input_response") {
            std::string payload = msg.value("payload", "");
            bridgeTerminal->EnqueueInputResponse(payload);
        }
    });

    bridge.on_error([bridgeTerminal](yuumi::Error) {
        bridgeTerminal->RequestShutdown();
    });

    if (auto res = bridge.start(*pipeArg, 0); !res) {
        std::fprintf(stderr, "[ZERI_ENGINE] bridge.start() failed: %s\n",
            std::string(yuumi::to_string(res.error())).c_str());
        std::fflush(stderr);
        return 1;
    }

    nlohmann::json handshakeMsg;
    handshakeMsg["type"] = Zeri::Ui::kBridgeTypeHandshake;
    handshakeMsg["protocol_version"] = Zeri::Ui::ZERI_PROTOCOL_VERSION;
    sinkOwner->Send(handshakeMsg);

    sinkOwner->SetConnected(true);
    sinkOwner->Flush();

    Zeri::Core::RuntimeState runtimeState;
    Zeri::Engines::Defaults::MetaParser parser;
    Zeri::Engines::Defaults::DefaultDispatcher baseDispatcher;
    Zeri::Engines::Defaults::CachedDispatcher dispatcher(parser, baseDispatcher);
    std::vector<Zeri::Core::BugSnapshotCommandRecord> commandHistory;

    runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::GlobalContext>());

    if (runtimeState.WasSessionCorrupted()) {
        terminalOwner->WriteError(
            "[ZERI][SESSION-003] Previous session file was corrupted or could not be loaded. "
            "Hint: run /save to write a fresh session snapshot."
        );
    }

    nlohmann::json readyMsg;
    readyMsg["type"] = "ready";
    sinkOwner->Send(readyMsg);

    Zeri::Ui::ITerminal& terminal = *terminalOwner;
    EmitStartupDiagnostics(startupDiagnostics, &terminal);

    if (!Zeri::Core::HelpCatalog::Instance().IsLoaded()) {
        const auto& error = Zeri::Core::HelpCatalog::Instance().LastError();
        const std::string details = error.empty() ? "No additional diagnostics available." : error;
        terminal.WriteError(
            "[ZERI][CONTEXT-005] Help catalog is unavailable, /help output may be incomplete. "
            "Hint: package help/help_catalog.json next to the executable. "
            "Details: " + details
        );
    }

    runtimeState.GetCurrentContext()->OnEnter(terminal);
    if (const auto persistencePaths = TryCollectPersistencePaths(); persistencePaths.has_value()) {
        std::string welcomeMessage = "Persistence paths\n";
        welcomeMessage += BuildPersistencePathsBlock(*persistencePaths, "  ");
        terminal.WriteInfo(welcomeMessage);
    } else {
        terminal.WriteInfo("Persistence paths unavailable: unable to resolve user data directory from environment.");
    }

    while (!runtimeState.IsExitRequested()) {
        auto* currentCtx = runtimeState.GetCurrentContext();
        std::string prompt = currentCtx ? (currentCtx->GetPrompt() + "> ") : "zeri> ";

        auto inputOpt = terminal.ReadLine(prompt);
        if (!inputOpt.has_value()) break;

        std::string input = *inputOpt;
        if (Trim(input).empty()) continue;

        auto* activeContext = runtimeState.GetCurrentContext();
        if (activeContext != nullptr && activeContext->WantsRawInput()) {
            auto outcome = activeContext->HandleRawLine(input, runtimeState, terminal);
            HandleOutcome(outcome, terminal);
            AppendCommandHistory(commandHistory, input, outcome.has_value(), outcome.has_value() ? "OK" : outcome.error().code);
            bridgeTerminal->EmitBatchEnd(Zeri::Ui::kBatchEndExecutionComplete);
            continue;
        }

        const std::string lowered = ToLower(input);
        if (lowered == "$code") {
            (void)SwitchContext("code", runtimeState, terminal, sinkOwner.get());
            AppendCommandHistory(commandHistory, input, true, "CONTEXT_SWITCH");
            bridgeTerminal->EmitBatchEnd(Zeri::Ui::kBatchEndExecutionComplete);
            continue;
        }
        if (lowered == "$customcommand") {
            (void)SwitchContext("customcommand", runtimeState, terminal, sinkOwner.get());
            AppendCommandHistory(commandHistory, input, true, "CONTEXT_SWITCH");
            bridgeTerminal->EmitBatchEnd(Zeri::Ui::kBatchEndExecutionComplete);
            continue;
        }

        bool ok = ExecuteStage(input, dispatcher, runtimeState, terminal, sinkOwner.get(), &startupDiagnostics, &commandHistory);
        AppendCommandHistory(commandHistory, input, ok, ok ? "OK" : "FAILED");
        bridgeTerminal->EmitBatchEnd(Zeri::Ui::kBatchEndExecutionComplete);

        if (!ok) {
            continue;
        }
    }

    bridgeTerminal->EmitBatchEnd(Zeri::Ui::kBatchEndEngineShutdown);
    terminal.WriteLine("Goodbye.");
    auto sessionPath = Zeri::Core::ResolveSessionPath();
    auto saveResult = runtimeState.SaveSession(sessionPath);
    if (!saveResult.has_value()) {
        terminal.WriteError("[ZERI][SESSION-004] Failed to save session on shutdown: " + saveResult.error() + ". Hint: run /status and ensure the sessions path is writable.");
    }
    return 0;
}

int main(int argc, char* argv[]) {
    try {
        return RunMain(argc, argv);
    } catch (const std::exception& ex) {
        std::fprintf(stderr, "[ZERI_ENGINE][FATAL] Unhandled exception: %s\n", ex.what());
        std::fflush(stderr);
        return 1;
    } catch (...) {
        std::fprintf(stderr, "[ZERI_ENGINE][FATAL] Unhandled non-standard exception.\n");
        std::fflush(stderr);
        return 1;
    }
}

/*
main.cpp — ZeriEngine entry point in bridge-only mode.

Execution model:
  - --yuumi-pipe <name> is mandatory.
  - The process initializes yuumi::Bridge, OutputSink, and BridgeTerminal.
  - RuntimeState and the REPL dispatch pipeline are started only after
    bridge.start() succeeds.

Transport model:
  - OutputSink is the single outbound transport abstraction.
  - Messages produced before connection are buffered and flushed when
    SetConnected(true) is applied.
  - Core code never sends directly through yuumi::Bridge.

Bridge protocol (high-level):
  - First output frame:
      {"type": "handshake", "protocol_version": 1}
  - Input from Go UI:
      {"type": "command", "payload": "<user input>"}
      {"type": "input_response", "payload": "<wizard reply>"}
  - Output to Go UI:
      {"type": "ready"}
      {"type": "output"|"error"|"info"|"success", "payload": "<text>"}
      {"type": "context_changed", "context": "<name>", "prompt": "<prompt>"}
      {"type": "req_input", "prompt": "<text>"}
      {"type": "stream_batch_end", "reason": "<execution_complete|before_input_request|context_transition|runtime_idle|engine_shutdown>"}

Behavior notes:
  - Global commands (/context, /back, /status, /reset, /bug report, /exit, /save) are
    handled centrally before delegating to the active context.
  - Context changes and reset events emit context_changed when a sink exists.
  - System command execution uses Zeri::Platform::POpen/PClose wrappers to keep
    cross-platform process pipe handling localized.
  - EmitStartupDiagnostics: "Executable directory" is always logged to stderr but
    is now only written to the TUI when there are actual startup issues. This removes
    the unconditional informational noise from the user-facing output on clean starts.
*/
