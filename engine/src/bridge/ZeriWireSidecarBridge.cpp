#include "ZeriWireSidecarBridge.h"

#include "../../../src/ZeriLink/Include/IProcessHost.h"

#include <chrono>
#include <condition_variable>
#include <nlohmann/json.hpp>
#include <optional>

namespace {

    inline constexpr std::chrono::seconds kLaunchHandshakeTimeout{ 5 };
    inline constexpr std::chrono::seconds kWatchdogTimeout{ 30 };
    inline constexpr std::chrono::seconds kExecuteCompletionTimeout{ 35 };
    inline constexpr const char* kDefaultInputPrompt = "Input: ";

}

namespace Zeri::Bridge {

    ZeriWireSidecarBridge::ZeriWireSidecarBridge() = default;

    ZeriWireSidecarBridge::~ZeriWireSidecarBridge() {
        Shutdown();
    }

    bool ZeriWireSidecarBridge::Launch(
        const std::string& executablePath,
        const std::vector<std::string>& args,
        const std::string& bootstrapPath
    ) {
        std::lock_guard lock(m_stateMutex);

        if (m_alive) {
            return false;
        }

        auto host = Zeri::Link::IProcessHost::CreateProcessHost();
        if (!host) {
            return false;
        }

        auto sidecarBridge = std::make_unique<Zeri::Link::SidecarProcessBridge>(std::move(host));
        sidecarBridge->SetWatchdogTimeout(kWatchdogTimeout);

        std::vector<std::string> launchArgs = args;
        launchArgs.push_back(bootstrapPath);

        const bool launched = sidecarBridge->Launch(executablePath, launchArgs, kLaunchHandshakeTimeout);
        if (!launched) {
            m_bridge.reset();
            m_alive = false;
            return false;
        }

        m_bridge = std::move(sidecarBridge);
        m_alive = true;
        return true;
    }

    ExecutionResult ZeriWireSidecarBridge::Execute(const std::string& code, Zeri::Ui::ITerminal& terminal) {
        std::unique_lock executeLock(m_executeMutex);

        Zeri::Link::SidecarProcessBridge* bridge = nullptr;
        {
            std::lock_guard lock(m_stateMutex);
            if (!m_alive || !m_bridge) {
                return std::unexpected(Zeri::Engines::ExecutionError{
                    "SIDECAR_STATE_ERR",
                    "Sidecar process is not running.",
                    std::nullopt,
                    { "Launch the sidecar bridge before Execute." }
                });
            }
            bridge = m_bridge.get();
        }

        bridge->SetInputRequestHandler([bridge, &terminal](const std::string& payload) {
            HandleInputRequest(*bridge, payload, terminal);
        });

        std::mutex resultMutex;
        std::condition_variable resultCv;
        bool completed = false;
        ExecutionResult executionResult = std::unexpected(Zeri::Engines::ExecutionError{
            "SIDECAR_PROTOCOL_ERR",
            "Sidecar returned no execution result.",
            std::nullopt,
            { "Check sidecar process logs and protocol framing." }
        });

        bridge->ExecuteCode(BuildExecCodePayload(code), [&](const Zeri::Link::ZeriFrame& frame) {
            ExecutionResult parsed = ParseExecutionResultPayload(frame.payload);
            {
                std::lock_guard lock(resultMutex);
                executionResult = std::move(parsed);
                completed = true;
            }
            resultCv.notify_one();
        });

        {
            std::unique_lock lock(resultMutex);
            const bool signaled = resultCv.wait_for(lock, kExecuteCompletionTimeout, [&completed] {
                return completed;
            });

            if (!signaled) {
                bridge->SetInputRequestHandler({});
                return std::unexpected(Zeri::Engines::ExecutionError{
                    "SIDECAR_WAIT_TIMEOUT",
                    "Timed out while waiting for sidecar execution result.",
                    std::nullopt,
                    { "Verify bootstrap readiness and sidecar responsiveness." }
                });
            }
        }

        bridge->SetInputRequestHandler({});

        if (!executionResult.has_value()) {
            if (executionResult.error().code == "SIDECAR_TIMEOUT") {
                std::lock_guard lock(m_stateMutex);
                m_alive = false;
            }
        }

        return executionResult;
    }

    void ZeriWireSidecarBridge::Shutdown() {
        std::unique_lock executeLock(m_executeMutex);

        std::unique_ptr<Zeri::Link::SidecarProcessBridge> bridge;
        {
            std::lock_guard lock(m_stateMutex);
            bridge = std::move(m_bridge);
            m_alive = false;
        }

        if (bridge) {
            bridge->Shutdown();
        }
    }

    bool ZeriWireSidecarBridge::IsAlive() const {
        std::lock_guard lock(m_stateMutex);
        return m_alive && static_cast<bool>(m_bridge);
    }

    std::string ZeriWireSidecarBridge::BuildExecCodePayload(const std::string& code) {
        return "{\"code\":\"" + EscapeJsonString(code) + "\"}";
    }

    std::string ZeriWireSidecarBridge::BuildInputResponsePayload(const std::string& value) {
        return "{\"value\":\"" + EscapeJsonString(value) + "\"}";
    }

    std::string ZeriWireSidecarBridge::EscapeJsonString(const std::string& value) {
        std::string escaped;
        escaped.reserve(value.size());

        for (const unsigned char ch : value) {
            switch (ch) {
            case '"':
                escaped += "\\\"";
                break;
            case '\\':
                escaped += "\\\\";
                break;
            case '\b':
                escaped += "\\b";
                break;
            case '\f':
                escaped += "\\f";
                break;
            case '\n':
                escaped += "\\n";
                break;
            case '\r':
                escaped += "\\r";
                break;
            case '\t':
                escaped += "\\t";
                break;
            default:
                if (ch < 0x20u) {
                    static constexpr char kHexDigits[] = "0123456789abcdef";
                    escaped += "\\u00";
                    escaped += kHexDigits[(ch >> 4u) & 0x0Fu];
                    escaped += kHexDigits[ch & 0x0Fu];
                }
                else {
                    escaped.push_back(static_cast<char>(ch));
                }
                break;
            }
        }

        return escaped;
    }

    ExecutionResult ZeriWireSidecarBridge::ParseExecutionResultPayload(const std::string& payload) {
        const auto json = nlohmann::json::parse(payload, nullptr, false);
        if (json.is_discarded() || !json.is_object()) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "SIDECAR_PROTOCOL_ERR",
                "Invalid EXEC_RESULT payload received from sidecar.",
                payload,
                { "Verify sidecar bootstrap output uses valid JSON." }
            });
        }

        SidecarExecutionPayload result{};

        if (json.contains("output") && json["output"].is_string()) {
            result.stdoutText = json["output"].get<std::string>();
        }

        if (json.contains("error") && json["error"].is_string()) {
            result.stderrText = json["error"].get<std::string>();
        }

        if (json.contains("exitCode") && json["exitCode"].is_number_integer()) {
            result.exitCode = json["exitCode"].get<int>();
        }
        else {
            result.exitCode = -1;
        }

        if (result.stderrText == "Execution timed out" || result.exitCode == -1 && result.stderrText.find("timed out") != std::string::npos) {
            return std::unexpected(Zeri::Engines::ExecutionError{
                "SIDECAR_TIMEOUT",
                "Execution exceeded watchdog timeout and was interrupted.",
                std::nullopt,
                { "Reduce script runtime or increase sidecar watchdog timeout." }
            });
        }

        return result;
    }

    void ZeriWireSidecarBridge::HandleInputRequest(
        Zeri::Link::SidecarProcessBridge& bridge,
        const std::string& payload,
        Zeri::Ui::ITerminal& terminal
    ) {
        std::string promptText = kDefaultInputPrompt;

        const auto json = nlohmann::json::parse(payload, nullptr, false);
        if (!json.is_discarded() && json.is_object() && json.contains("prompt") && json["prompt"].is_string()) {
            promptText = json["prompt"].get<std::string>();
        }

        const std::optional<std::string> input = terminal.ReadLine(promptText);
        bridge.SendInputResponse(BuildInputResponsePayload(input.value_or(std::string{})));
    }

}

/*
ZeriWireSidecarBridge wraps SidecarProcessBridge without subclassing executor
interfaces. Launch creates a platform-specific host through IProcessHost and
reuses existing SidecarProcessBridge handshake, watchdog, and cleanup behavior.

Execute issues EXEC_CODE, blocks until EXEC_RESULT, forwards REQ_INPUT events to
ITerminal::ReadLine, and sends RES_INPUT frames back to the running sidecar.
Payload conversion is explicit and typed through std::expected so callers can
handle protocol, state, timeout, and synchronization failures deterministically.

Shutdown delegates to SidecarProcessBridge::Shutdown and releases ownership,
ensuring process tree cleanup remains centralized in existing bridge internals.
*/
