#include "Ui/Include/BridgeTerminal.h"

namespace Zeri::Ui {

    BridgeTerminal::BridgeTerminal(OutputSink& sink)
        : m_sink(sink)
    {
    }

    void BridgeTerminal::SendOutput(const std::string& type, const std::string& payload) {
        m_sink.Send(type, payload);
    }

    void BridgeTerminal::Write(const std::string& text) {
        SendOutput("output", text);
    }

    void BridgeTerminal::WriteLine(const std::string& text) {
        SendOutput("output", text);
    }

    void BridgeTerminal::WriteError(const std::string& text) {
        SendOutput("error", text);
    }

    void BridgeTerminal::WriteSuccess(const std::string& text) {
        SendOutput("success", text);
    }

    void BridgeTerminal::WriteInfo(const std::string& text) {
        SendOutput("info", text);
    }

    std::optional<std::string> BridgeTerminal::ReadLine(const std::string& prompt) {
        int depth = m_readDepth.fetch_add(1, std::memory_order_acq_rel);

        if (depth > 0) {
            nlohmann::json reqMsg;
            reqMsg["type"] = "req_input";
            reqMsg["prompt"] = prompt;
            m_sink.Send(reqMsg);

            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [this] {
                return !m_inputQueue.empty() || m_shutdown.load(std::memory_order_acquire);
                });

            m_readDepth.fetch_sub(1, std::memory_order_acq_rel);

            if (m_shutdown.load(std::memory_order_acquire)) return std::nullopt;

            auto response = std::move(m_inputQueue.front());
            m_inputQueue.pop();
            return response;
        }

        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] {
            return !m_commandQueue.empty() || m_shutdown.load(std::memory_order_acquire);
            });

        m_readDepth.fetch_sub(1, std::memory_order_acq_rel);

        if (m_shutdown.load(std::memory_order_acquire)) return std::nullopt;

        auto command = std::move(m_commandQueue.front());
        m_commandQueue.pop();
        return command;
    }

    bool BridgeTerminal::Confirm(const std::string& prompt, bool /*default_value*/) {
        auto response = ReadLine(prompt + " [y/n]: ");
        if (!response.has_value()) return false;
        return response->starts_with("y") || response->starts_with("Y");
    }

    std::optional<int> BridgeTerminal::SelectMenu(const std::string& title, const std::vector<std::string>& options) {
        nlohmann::json req;
        req["type"] = "sel_request";
        req["title"] = title;
        req["options"] = options;
        m_sink.Send(req);

        std::unique_lock lock(m_mutex);
        m_cv.wait(lock, [this] {
            return !m_inputQueue.empty() || m_shutdown.load(std::memory_order_acquire);
        });

        if (m_shutdown.load(std::memory_order_acquire)) return std::nullopt;

        auto response = std::move(m_inputQueue.front());
        m_inputQueue.pop();

        try {
            int idx = std::stoi(response);
            if (idx >= 0 && idx < static_cast<int>(options.size())) return idx;
        } catch (...) {}
        return std::nullopt;
    }

    void BridgeTerminal::EnqueueCommand(const std::string& payload) {
        std::lock_guard lock(m_mutex);
        m_commandQueue.push(payload);
        m_cv.notify_one();
    }

    void BridgeTerminal::EnqueueInputResponse(const std::string& payload) {
        std::lock_guard lock(m_mutex);
        m_inputQueue.push(payload);
        m_cv.notify_one();
    }

    void BridgeTerminal::RequestShutdown() {
        m_shutdown.store(true, std::memory_order_release);
        m_cv.notify_all();
    }

}

/*
BridgeTerminal.cpp — Implementation of the headless ITerminal.

All outbound messages are routed through OutputSink rather than calling
bridge.send() directly. OutputSink handles connection-state awareness:
messages produced before the handshake are buffered and flushed once the
bridge is marked connected. This eliminates the risk of lost output during
the boot window.

SendOutput builds a {"type": ..., "payload": ...} JSON envelope and
forwards it to OutputSink::Send(), which either delivers immediately or
buffers depending on connection state.

ReadLine implements depth-tracked blocking reads:
  - Depth 0 (first call from REPL loop): waits on m_commandQueue for the
    next "command" message from the TUI.
  - Depth >0 (nested call from a command handler): sends a "req_input"
    JSON prompt through the sink and blocks on m_inputQueue until an
    "input_response" arrives.

m_readDepth is an atomic counter. Each ReadLine entry increments it; each
exit decrements it. The first call always sees depth==0 (command queue).
Any nested call sees depth>0 (input queue).

Confirm delegates to ReadLine with a formatted prompt and parses the
response for y/Y.

EnqueueCommand and EnqueueInputResponse are called from the bridge's
on_message callback (reader thread). They push into the respective queue
and notify the condition variable so ReadLine unblocks on the main thread.

RequestShutdown sets the atomic flag and wakes all waiters, ensuring the
main loop's ReadLine returns nullopt and the process exits cleanly.
*/