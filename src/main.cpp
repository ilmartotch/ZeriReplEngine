#include "Core/Include/RuntimeState.h"
#include "Core/Include/HelpCatalog.h"
#include "Core/Include/SystemGuard.h"
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
#include "Ui/Include/TerminalUi.h"
#include "Ui/Include/BridgeTerminal.h"
#include "Ui/Include/OutputSink.h"
#include "Engines/Include/MetaParser.h"
#include "Engines/Include/DefaultDispatcher.h"
#include "Engines/Include/CachedDispatcher.h"

#include <yuumi/bridge.hpp>

#include <memory>
#include <format>
#include <array>
#include <cctype>
#include <cstdio>
#include <string_view>
#include <vector>
#include <optional>

namespace {
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

    [[nodiscard]] std::vector<std::string> SplitPipeline(std::string_view input) {
        std::vector<std::string> stages;
        std::string current;
        bool inQuotes = false;
        bool escape = false;

        for (char c : input) {
            if (escape) {
                current.push_back(c);
                escape = false;
                continue;
            }

            if (c == '\\') {
                escape = true;
                current.push_back(c);
                continue;
            }

            if (c == '"') {
                inQuotes = !inQuotes;
                current.push_back(c);
                continue;
            }

            if (c == '|' && !inQuotes) {
                stages.emplace_back(std::string{ Trim(current) });
                current.clear();
                continue;
            }

            current.push_back(c);
        }

        stages.emplace_back(std::string{ Trim(current) });
        return stages;
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
                    terminal.WriteError(text);
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
            terminal.WriteError(formatted);
        }
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
                if (sink) {
                    nlohmann::json msg;
                    msg["type"] = "context_changed";
                    msg["context"] = activeContext->GetName();
                    msg["prompt"] = activeContext->GetPrompt();
                    sink->Send(msg);
                }
                return true;
            }

            terminal.WriteError("Internal error: no active context available.");
            return false;
        }

        const auto* current = runtimeState.GetCurrentContext();
        const std::string currentName = current ? ToLower(current->GetName()) : std::string{ "global" };
        if (!CanReachContext(currentName, normalized)) {
            terminal.WriteError(
                "Context switch not allowed from $" + currentName + " to $" + normalized +
                ". Use /context to list reachable contexts."
            );
            return false;
        }

        auto nextContext = BuildContext(normalized);
        if (!nextContext) {
            terminal.WriteError("Unknown context: " + ctxName);
            return false;
        }

        runtimeState.PushContext(std::move(nextContext));
        auto* pushed = runtimeState.GetCurrentContext();
        pushed->OnEnter(terminal);
        if (sink) {
            nlohmann::json msg;
            msg["type"] = "context_changed";
            msg["context"] = pushed->GetName();
            msg["prompt"] = pushed->GetPrompt();
            sink->Send(msg);
        }
        return true;
    }

    [[nodiscard]] bool HandleGlobalCommand(
        const Zeri::Engines::Command& cmd,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal,
        Zeri::Ui::OutputSink* sink = nullptr
    ) {
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
                if (sink) {
                    nlohmann::json msg;
                    msg["type"] = "context_changed";
                    msg["context"] = parent->GetName();
                    msg["prompt"] = parent->GetPrompt();
                    sink->Send(msg);
                }
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
            result += "  Fn rev:     " + std::to_string(runtimeState.GetFunctionRegistryRevision());
            terminal.WriteLine(result);
            return true;
        }

        if (cmd.commandName == "reset") {
            runtimeState.ResetSession();

            auto* ctx = runtimeState.GetCurrentContext();
            const std::string ctxName = ctx ? ctx->GetName() : "global";

            terminal.WriteSuccess("Session reset. Context: " + ctxName);

            if (sink) {
                nlohmann::json msg;
                msg["type"] = "context_changed";
                msg["context"] = ctxName;
                msg["prompt"] = ctx ? ctx->GetPrompt() : "zeri";
                sink->Send(msg);
            }
            return true;
        }

        if (cmd.commandName == "exit") {
            runtimeState.RequestExit();
            terminal.WriteInfo("Exiting...");
            return true;
        }

        if (cmd.commandName == "save") {
            auto result = runtimeState.SaveSession(".zeri/state.json");
            if (result.has_value()) {
                terminal.WriteSuccess("Session saved successfully.");
            } else {
                terminal.WriteError("Failed to save session: " + result.error());
            }
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
            terminal.WriteError("Empty system command.");
            return false;
        }

#ifdef _WIN32
        std::string fullCmd = shellCmd + " 2>&1";
#else
        std::string fullCmd = shellCmd + " 2>&1";
#endif

        std::array<char, 256> buffer{};
        std::string output;

#ifdef _WIN32
        FILE* pipe = _popen(fullCmd.c_str(), "r");
#else
        FILE* pipe = popen(fullCmd.c_str(), "r");
#endif

        if (!pipe) {
            terminal.WriteError("Failed to execute system command: " + shellCmd);
            return false;
        }

        while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr) {
            output += buffer.data();
        }

#ifdef _WIN32
        int exitCode = _pclose(pipe);
#else
        int exitCode = pclose(pipe);
#endif

        if (!output.empty()) {
            if (output.back() == '\n') output.pop_back();
            terminal.WriteLine(output);
        } else if (exitCode == 0) {
            terminal.WriteInfo("(command completed with no output)");
        }

        if (exitCode != 0) {
            terminal.WriteError("System command exited with code: " + std::to_string(exitCode));
            return false;
        }

        return true;
    }

    [[nodiscard]] bool ExecuteStage(
        const std::string& stageInput,
        Zeri::Engines::Defaults::CachedDispatcher& dispatcher,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal,
        std::optional<std::string>& pipedValue,
        Zeri::Ui::OutputSink* sink = nullptr
    ) {
        auto dispatchResult = dispatcher.Dispatch(stageInput);
        if (!dispatchResult.has_value()) {
            const auto& err = dispatchResult.error();
            std::string caret(err.position, ' ');
            caret += '^';
            terminal.WriteError(err.message + "\n  " + stageInput + "\n  " + caret);
            return false;
        }

        auto cmd = dispatchResult->command;
        if (cmd.empty()) {
            terminal.WriteInfo("(empty input — no operation)");
            return true;
        }

        cmd.pipeInput = pipedValue;

        switch (cmd.type) {
        case Zeri::Engines::InputType::ContextSwitch:
            return SwitchContext(cmd.commandName, runtimeState, terminal, sink);

        case Zeri::Engines::InputType::SystemOp:
            return ExecuteSystemOp(cmd, terminal);

        case Zeri::Engines::InputType::Expression:
        case Zeri::Engines::InputType::Command: {
            auto* currentCtx = runtimeState.GetCurrentContext();
            if (!currentCtx) {
                terminal.WriteError("No active context available.");
                return false;
            }

            if (cmd.type == Zeri::Engines::InputType::Command &&
                currentCtx->IsGlobalCommand(cmd.commandName)) {
                if (HandleGlobalCommand(cmd, runtimeState, terminal, sink)) {
                    return true;
                }
            }

            auto outcome = currentCtx->HandleCommand(cmd, runtimeState, terminal);
            HandleOutcome(outcome, terminal);

            if (!outcome.has_value()) return false;
            pipedValue = outcome.value().text;
            return true;
        }

        default:
            terminal.WriteError("Unrecognized input type.");
            return false;
        }
    }

    [[nodiscard]] bool TryExecuteInlineSandbox(
        std::string_view input,
        Zeri::Engines::Defaults::CachedDispatcher& dispatcher,
        Zeri::Core::RuntimeState& runtimeState,
        Zeri::Ui::ITerminal& terminal
    ) {
        const std::string_view trimmed = Trim(input);
        if (!ToLower(trimmed).starts_with("$sandbox")) {
            return false;
        }

        auto dispatchResult = dispatcher.Dispatch(std::string(trimmed));
        if (!dispatchResult.has_value()) {
            return false;
        }

        const auto& cmd = dispatchResult->command;
        if (cmd.type != Zeri::Engines::InputType::ContextSwitch ||
            cmd.commandName != "sandbox" ||
            !cmd.pipeInput.has_value()) {
            return false;
        }

        Zeri::Engines::Defaults::SandboxContext sandbox;
        Zeri::Engines::Command sandboxCommand;
        sandboxCommand.type = Zeri::Engines::InputType::Expression;
        sandboxCommand.rawInput = *cmd.pipeInput;
        sandboxCommand.commandName = "@expr";
        sandboxCommand.args.push_back(*cmd.pipeInput);

        auto outcome = sandbox.HandleCommand(sandboxCommand, runtimeState, terminal);
        HandleOutcome(outcome, terminal);
        return true;
    }
}

namespace {
    [[nodiscard]] std::optional<std::string> ParsePipeArg(int argc, char* argv[]) {
        for (int i = 1; i < argc - 1; ++i) {
            if (std::string_view(argv[i]) == "--yuumi-pipe") {
                return std::string(argv[i + 1]);
            }
        }
        return std::nullopt;
    }
}

int main(int argc, char* argv[]) {
    auto pipeArg = ParsePipeArg(argc, argv);
    bool headless = pipeArg.has_value();

    std::unique_ptr<Zeri::Ui::ITerminal> terminalOwner;
    std::unique_ptr<yuumi::Bridge> bridgeOwner;
    std::unique_ptr<Zeri::Ui::OutputSink> sinkOwner;
    Zeri::Ui::BridgeTerminal* bridgeTerminal = nullptr;

    if (headless) {
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

        sinkOwner->SetConnected(true);
        sinkOwner->Flush();
    }

    Zeri::Core::RuntimeState runtimeState;
    Zeri::Engines::Defaults::MetaParser parser;
    Zeri::Engines::Defaults::DefaultDispatcher baseDispatcher;
    Zeri::Engines::Defaults::CachedDispatcher dispatcher(parser, baseDispatcher);

    if (headless) {
        runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::GlobalContext>());

        nlohmann::json readyMsg;
        readyMsg["type"] = "ready";
        sinkOwner->Send(readyMsg);

    } else {
        terminalOwner = std::make_unique<Zeri::Ui::TerminalUi>();
    }

    Zeri::Ui::ITerminal& terminal = *terminalOwner;

    if (!headless) {
        auto health = Zeri::Core::SystemGuard::CheckEnvironment();
        if (!health.IsReady()) {
            Zeri::Core::SystemGuard::PrintGuide(health, terminal);
        }

        runtimeState.PushContext(std::make_unique<Zeri::Engines::Defaults::GlobalContext>());
        runtimeState.GetCurrentContext()->OnEnter(terminal);
    }

    while (!runtimeState.IsExitRequested()) {
        auto* currentCtx = runtimeState.GetCurrentContext();
        std::string prompt = currentCtx ? (currentCtx->GetPrompt() + "> ") : "zeri> ";

        auto inputOpt = terminal.ReadLine(prompt);
        if (!inputOpt.has_value()) break;

        std::string input = *inputOpt;
        if (Trim(input).empty()) continue;

        if (auto* editor = dynamic_cast<Zeri::Engines::ScriptEditorContext*>(runtimeState.GetCurrentContext())) {
            Zeri::Engines::Command rawCmd;
            rawCmd.rawInput = input;
            rawCmd.type = Zeri::Engines::InputType::Expression;
            auto outcome = editor->HandleCommand(rawCmd, runtimeState, terminal);
            HandleOutcome(outcome, terminal);
            continue;
        }

        const std::string lowered = ToLower(input);
        if (lowered == "$code") {
            (void)SwitchContext("code", runtimeState, terminal, sinkOwner.get());
            continue;
        }
        if (lowered == "$customcommand") {
            (void)SwitchContext("customcommand", runtimeState, terminal, sinkOwner.get());
            continue;
        }

        if (TryExecuteInlineSandbox(input, dispatcher, runtimeState, terminal)) {
            continue;
        }

        auto stages = SplitPipeline(input);
        if (std::ranges::any_of(stages, [](const std::string& s) { return Trim(s).empty(); })) {
            terminal.WriteError("Invalid pipeline syntax: empty stage detected around '|'.");
            continue;
        }

        std::optional<std::string> pipedValue;
        bool ok = true;
        for (const auto& stage : stages) {
            if (!ExecuteStage(stage, dispatcher, runtimeState, terminal, pipedValue, sinkOwner.get())) {
                ok = false;
                break;
            }
        }

        if (!ok) {
            continue;
        }
    }

    terminal.WriteLine("Goodbye.");
    return 0;
}

/*
main.cpp — ZeriEngine entry point with bridge-first headless architecture.

Interactive mode (no arguments):
  TerminalUi provides console I/O via replxx (history, highlighting,
  completions). This is the standard standalone REPL experience.

Headless mode (--yuumi-pipe <name>):
  The IPC bridge is established BEFORE any REPL state is initialized.
  Initialization order in headless mode:
    1. Parse --yuumi-pipe argument
    2. Create yuumi::Bridge and OutputSink (transport abstraction)
    3. Create BridgeTerminal (ITerminal over the sink)
    4. Register on_message / on_error handlers
    5. bridge.start() — blocks until handshake completes (Handshake Protocol)
    6. SetConnected(true) + Flush() — drain any buffered boot messages
    7. Create RuntimeState, parser, dispatcher (REPL initialization)
    8. Push GlobalContext, send "ready" through the sink
    9. Enter shared REPL loop

  Bridge-First guarantee: no REPL state exists until the transport layer
  has a verified peer connection. This prevents output loss during boot.

  Transport Abstraction: OutputSink wraps bridge.send() with connection
  awareness. Before the handshake, messages are buffered internally.
  After SetConnected(true), messages flow directly to the bridge.
  No component in the core calls bridge.send() directly.

  The headless path produces zero writes to stdout or stderr. All output
  is structured JSON routed through OutputSink -> yuumi::Bridge -> TUI.

Headless message protocol:
  TUI -> Engine:
    {"type": "command", "payload": "<user input>"}
    {"type": "input_response", "payload": "<wizard reply>"}
  Engine -> TUI:
    {"type": "ready"}
    {"type": "output", "payload": "<text>"}
    {"type": "error", "payload": "<text>"}
    {"type": "info", "payload": "<text>"}
    {"type": "success", "payload": "<text>"}
    {"type": "context_changed", "context": "<name>", "prompt": "<prompt>"}
    {"type": "req_input", "prompt": "<text>"}

The main REPL loop is shared between both modes: ReadLine blocks on stdin
(interactive) or on the command queue (headless), and the dispatch pipeline
is identical in both cases.

QA Changes:
  - SwitchContext(), HandleGlobalCommand(), ExecuteStage() now accept an
    optional OutputSink* parameter. When non-null (headless mode), a
    context_changed JSON frame is sent after every successful context switch,
    /back pop, and /reset.
  - /status handler: returns current context name, local variable count,
    local function count, and function registry revision.
  - /reset handler: calls RuntimeState::ResetSession() to clear local/session
    variables and functions, pops all contexts back to global, then sends
    context_changed.
  - mode_request handler removed from bridge.on_message (no longer needed
    after removing /lang).
  - ToLower() helper: normalizes commands and context names to lowercase,
    satisfying the meta-language spec requirement for case-insensitive matching.
  - HandleGlobalCommand(): routes /context, /back, /status, /reset, /exit,
    /save from any sub-context before delegating to the active context.
  - ExecuteSystemOp(): executes ! prefixed system commands via popen/_popen.
  - HandleOutcome(): semantic formatting — confirmations route through
    WriteSuccess, data through WriteLine, errors through WriteError.
*/