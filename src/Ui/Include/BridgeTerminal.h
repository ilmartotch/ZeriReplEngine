#pragma once
#include "ITerminal.h"
#include "OutputSink.h"

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <string>
#include <optional>

namespace Zeri::Ui {

    class BridgeTerminal : public ITerminal {
    public:
        explicit BridgeTerminal(OutputSink& sink);
        ~BridgeTerminal() override = default;

        BridgeTerminal(const BridgeTerminal&) = delete;
        BridgeTerminal& operator=(const BridgeTerminal&) = delete;

        void Write(const std::string& text) override;
        void WriteLine(const std::string& text) override;
        void WriteError(const std::string& text) override;
        void WriteSuccess(const std::string& text) override;
        void WriteInfo(const std::string& text) override;
        [[nodiscard]] std::optional<std::string> ReadLine(const std::string& prompt) override;

        [[nodiscard]] bool Confirm(const std::string& prompt, bool default_value = true) override;
        [[nodiscard]] std::optional<int> SelectMenu(const std::string& title, const std::vector<std::string>& options) override;

        void EnqueueCommand(const std::string& payload);
        void EnqueueInputResponse(const std::string& payload);
        void RequestShutdown();

        [[nodiscard]] OutputSink& GetSink() { return m_sink; }

    private:
        void SendOutput(const std::string& type, const std::string& payload);

        OutputSink& m_sink;

        std::queue<std::string> m_commandQueue;
        std::queue<std::string> m_inputQueue;
        std::mutex m_mutex;
        std::condition_variable m_cv;
        std::atomic<bool> m_shutdown{ false };
        std::atomic<int> m_readDepth{ 0 };
    };

}

/*
BridgeTerminal.h — ITerminal implementation for headless (TUI-driven) mode.

When ZeriEngine is launched by zeri-tui.exe with --yuumi-pipe, all user
interaction must flow through the yuumi named-pipe bridge instead of
stdout/stdin. BridgeTerminal replaces TerminalUi as the ITerminal
implementation in this scenario.

All outbound messages are routed through an OutputSink reference rather
than calling bridge.send() directly. OutputSink provides connection-aware
buffering: messages produced before the handshake completes are buffered
and flushed once the bridge is marked connected. This decouples the
engine's output timeline from the transport's connection lifecycle.

Two internal queues separate the inbound message types:
  - m_commandQueue: filled by EnqueueCommand when the TUI sends a "command"
    message. Consumed by ReadLine in the main REPL loop (depth 0).
  - m_inputQueue: filled by EnqueueInputResponse when the TUI replies to a
    "req_input" prompt. Consumed by ReadLine when a command handler asks
    for interactive input (depth 1+, tracked by m_readDepth).

m_readDepth is an atomic counter incremented on each ReadLine entry and
decremented on exit. When the counter is >0 at entry, the call is nested
inside a command handler, so ReadLine sends a "req_input" message via the
sink and waits on m_inputQueue instead of m_commandQueue.

GetSink() exposes the OutputSink to main.cpp for sending protocol-level
messages (e.g. "ready", "mode_confirmed") that are not part of the
ITerminal interface.

RequestShutdown unblocks any thread waiting in ReadLine so the main loop
can exit cleanly when the TUI disconnects.
*/