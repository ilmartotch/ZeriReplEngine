#pragma once

#include <yuumi/bridge.hpp>

#include <vector>
#include <mutex>
#include <atomic>
#include <string>

namespace Zeri::Ui {

    class OutputSink {
    public:
        explicit OutputSink(yuumi::Bridge& bridge);
        ~OutputSink() = default;

        OutputSink(const OutputSink&) = delete;
        OutputSink& operator=(const OutputSink&) = delete;

        void Send(const std::string& type, const std::string& payload);
        void Send(const nlohmann::json& message, yuumi::Channel channel = yuumi::Channel::Command);

        void SetConnected(bool connected);
        [[nodiscard]] bool IsConnected() const;

        void Flush();

    private:
        yuumi::Bridge& m_bridge;
        std::atomic<bool> m_connected{ false };
        std::vector<std::pair<nlohmann::json, yuumi::Channel>> m_buffer;
        std::mutex m_mutex;
    };

}

/*
OutputSink.h — Transport abstraction for headless output routing.

OutputSink wraps the yuumi::Bridge and adds connection-awareness to
outbound message delivery. All engine output flows through this single
chokepoint instead of calling bridge.send() directly.

When the bridge has completed its handshake and SetConnected(true) is
called, Send() forwards structured JSON payloads immediately.

When the bridge is not yet connected (boot phase) or has been
disconnected (error), Send() appends messages to a small internal
buffer protected by a mutex. Once connectivity is restored and Flush()
is called, all buffered messages are drained in FIFO order.

This design satisfies the Transport Abstraction requirement: the core
engine never calls bridge.send() directly, and no output is silently
lost during the window between process launch and handshake completion.

Thread safety: m_connected is atomic; m_buffer is guarded by m_mutex.
Send() and Flush() may be called from any thread safely.
*/
