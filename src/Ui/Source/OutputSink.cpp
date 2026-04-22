#include "Ui/Include/OutputSink.h"

#include <utility>

namespace Zeri::Ui {

    OutputSink::OutputSink(yuumi::Bridge& bridge)
        : m_bridge(bridge)
    {
    }

    void OutputSink::Send(const std::string& type, const std::string& payload) {
        nlohmann::json msg;
        msg["type"] = type;
        msg["payload"] = payload;
        Send(msg, yuumi::Channel::Command);
    }

    void OutputSink::Send(const nlohmann::json& message, yuumi::Channel channel) {
        if (m_connected.load(std::memory_order_acquire)) {
            try {
                m_bridge.send(message, channel);
            } catch (...) {
                m_connected.store(false, std::memory_order_release);
                std::lock_guard lock(m_mutex);
                m_buffer.emplace_back(message, channel);
            }
            return;
        }

        std::lock_guard lock(m_mutex);
        m_buffer.emplace_back(message, channel);
    }

    void OutputSink::SetConnected(bool connected) {
        m_connected.store(connected, std::memory_order_release);
    }

    bool OutputSink::IsConnected() const {
        return m_connected.load(std::memory_order_acquire);
    }

    void OutputSink::Flush() {
        std::vector<std::pair<nlohmann::json, yuumi::Channel>> pending;

        {
            std::lock_guard lock(m_mutex);
            pending.swap(m_buffer);
        }

        for (size_t idx = 0; idx < pending.size(); ++idx) {
            const auto& [msg, ch] = pending[idx];
            try {
                m_bridge.send(msg, ch);
            } catch (...) {
                m_connected.store(false, std::memory_order_release);
                std::lock_guard lock(m_mutex);
                for (size_t rest = idx; rest < pending.size(); ++rest) {
                    m_buffer.emplace_back(std::move(pending[rest]));
                }
                break;
            }
        }
    }

}

/*
OutputSink.cpp — Implementation of the transport abstraction.

Send(type, payload) is the convenience overload used by BridgeTerminal.
It builds the JSON envelope and delegates to the general Send(json, channel).

The general Send() checks m_connected atomically:
  - If connected: forwards to bridge.send() immediately (zero-copy path).
  - If not connected: appends the message+channel pair to m_buffer under
    mutex protection. This prevents message loss during the boot window
    between process launch and handshake completion.

Flush() swaps the buffer out under the lock (minimizing critical section
duration) and then drains all messages in FIFO order through the bridge.
It is called once from main() immediately after SetConnected(true).

SetConnected / IsConnected use acquire/release memory ordering to ensure
that any thread observing m_connected==true also sees the fully initialized
bridge state that was established before the flag was set.
*/
