#pragma once

#include "../../../src/Engines/Include/ExecutionResult.h"
#include "../../../src/Ui/Include/ITerminal.h"
#include "../../../src/ZeriLink/Include/ProcessBridge.h"

#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace Zeri::Bridge {

    struct SidecarExecutionPayload {
        std::string stdoutText;
        std::string stderrText;
        int exitCode{ 0 };
    };

    using ExecutionResult = std::expected<SidecarExecutionPayload, Zeri::Engines::ExecutionError>;

    class ZeriWireSidecarBridge final {
    public:
        ZeriWireSidecarBridge();
        ~ZeriWireSidecarBridge();

        ZeriWireSidecarBridge(const ZeriWireSidecarBridge&) = delete;
        ZeriWireSidecarBridge& operator=(const ZeriWireSidecarBridge&) = delete;
        ZeriWireSidecarBridge(ZeriWireSidecarBridge&&) = delete;
        ZeriWireSidecarBridge& operator=(ZeriWireSidecarBridge&&) = delete;

        [[nodiscard]] bool Launch(
            const std::string& executablePath,
            const std::vector<std::string>& args,
            const std::string& bootstrapPath
        );

        [[nodiscard]] ExecutionResult Execute(const std::string& code, Zeri::Ui::ITerminal& terminal);

        void Shutdown();

        [[nodiscard]] bool IsAlive() const;

    private:
        [[nodiscard]] static std::string BuildExecCodePayload(const std::string& code);
        [[nodiscard]] static std::string BuildInputResponsePayload(const std::string& value);
        [[nodiscard]] static std::string EscapeJsonString(const std::string& value);

        [[nodiscard]] static ExecutionResult ParseExecutionResultPayload(const std::string& payload);
        static void HandleInputRequest(
            Zeri::Link::SidecarProcessBridge& bridge,
            const std::string& payload,
            Zeri::Ui::ITerminal& terminal
        );

        std::unique_ptr<Zeri::Link::SidecarProcessBridge> m_bridge;
        mutable std::mutex m_stateMutex;
        std::mutex m_executeMutex;
        bool m_alive{ false };
    };

}

/*
ZeriWireSidecarBridge composes SidecarProcessBridge and exposes a synchronous,
executor-oriented API based on Launch/Execute/Shutdown/IsAlive.

ExecutionResult intentionally uses std::expected<Payload, ExecutionError> to
follow the typed error pattern already used by the engine execution layer.
*/
