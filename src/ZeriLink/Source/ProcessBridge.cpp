#include "../Include/ProcessBridge.h"

namespace Zeri::Link {

    ProcessBridge::ProcessBridge(std::unique_ptr<IProcessHost> host)
        : m_host(std::move(host)) {}

    ProcessBridge::~ProcessBridge() {
        if (m_active.load()) {
            Shutdown();
        }
    }

    bool ProcessBridge::Launch(
        const std::string& executable,
        const std::vector<std::string>& args,
        std::chrono::seconds timeout
    ) {
        if (m_active.load()) return false;
        if (!m_host->Start(executable, args)) return false;

        m_ready.store(false);
        m_decoder.Reset();

        m_readerThread = std::jthread([this](std::stop_token token) {
            ReaderLoop(token);
        });

        m_watchdogThread = std::jthread([this](std::stop_token token) {
            WatchdogLoop(token);
        });

        std::unique_lock lock(m_readyMutex);
        bool ready = m_readyCv.wait_for(lock, timeout, [this] {
            return m_ready.load();
        });

        if (!ready) {
            Shutdown();
            return false;
        }

        m_active.store(true);
        return true;
    }

    void ProcessBridge::ExecuteCode(const std::string& jsonPayload, ResultCallback callback) {
        if (!m_active.load()) return;

        {
            std::lock_guard lock(m_callbackMutex);
            m_resultCallback = std::move(callback);
        }

        if (!SendFrame({ MsgType::EXEC_CODE, jsonPayload })) {
            ResultCallback cb;
            {
                std::lock_guard lock(m_callbackMutex);
                cb = std::move(m_resultCallback);
                m_resultCallback = nullptr;
            }
            if (cb) {
                cb({ MsgType::EXEC_RESULT,
                     R"({"output":"","error":"Failed to send code to sidecar","exitCode":-1})" });
            }
            return;
        }

        m_awaitingResult.store(true);
        m_watchdogCv.notify_one();
    }

    void ProcessBridge::SendInputResponse(const std::string& jsonPayload) {
        if (!m_active.load()) return;
        SendFrame({ MsgType::RES_INPUT, jsonPayload });
    }

    void ProcessBridge::Shutdown() {
        bool expected = true;
        if (!m_active.compare_exchange_strong(expected, false)) {
            if (m_readerThread.joinable()) {
                m_readerThread.request_stop();
            }
            if (m_watchdogThread.joinable()) {
                m_watchdogThread.request_stop();
                m_watchdogCv.notify_one();
            }
            m_host->Stop();
            if (m_readerThread.joinable()) m_readerThread.join();
            if (m_watchdogThread.joinable()) m_watchdogThread.join();
            return;
        }

        SendFrame({ MsgType::SYS_EVENT, R"({"status":"TERMINATE"})" });

        m_awaitingResult.store(false);

        if (m_watchdogThread.joinable()) {
            m_watchdogThread.request_stop();
            m_watchdogCv.notify_one();
        }

        if (m_readerThread.joinable()) {
            m_readerThread.request_stop();
        }

        m_host->Stop();

        if (m_watchdogThread.joinable()) m_watchdogThread.join();
        if (m_readerThread.joinable()) m_readerThread.join();

        m_decoder.Reset();

        {
            std::lock_guard lock(m_callbackMutex);
            m_resultCallback = nullptr;
        }
    }

    void ProcessBridge::SetInputRequestHandler(InputRequestCallback handler) {
        std::lock_guard lock(m_callbackMutex);
        m_inputHandler = std::move(handler);
    }

    void ProcessBridge::SetWatchdogTimeout(std::chrono::seconds timeout) {
        m_watchdogTimeout = timeout;
    }

    void ProcessBridge::ReaderLoop(std::stop_token token) {
        std::array<std::byte, 4096> buffer{};

        while (!token.stop_requested()) {
            size_t bytesRead = 0;
            if (!m_host->ReceiveData(buffer, bytesRead)) break;

            auto frame = m_decoder.Feed({ buffer.data(), bytesRead });
            while (frame.has_value()) {
                DispatchFrame(*frame);
                frame = m_decoder.Feed({});
            }
        }
    }

    void ProcessBridge::WatchdogLoop(std::stop_token token) {
        while (!token.stop_requested()) {
            std::unique_lock lock(m_watchdogMutex);

            m_watchdogCv.wait(lock, [&] {
                return m_awaitingResult.load() || token.stop_requested();
            });

            if (token.stop_requested()) break;

            bool resolved = m_watchdogCv.wait_for(lock, m_watchdogTimeout, [&] {
                return !m_awaitingResult.load() || token.stop_requested();
            });

            if (token.stop_requested()) break;

            if (!resolved) {
                m_awaitingResult.store(false);
                lock.unlock();

                SendFrame({ MsgType::SYS_EVENT,
                            R"({"status":"INTERRUPT","reason":"timeout"})" });

                ResultCallback cb;
                {
                    std::lock_guard cbLock(m_callbackMutex);
                    cb = std::move(m_resultCallback);
                    m_resultCallback = nullptr;
                }
                if (cb) {
                    cb({ MsgType::EXEC_RESULT,
                         R"({"output":"","error":"Execution timed out","exitCode":-1})" });
                }
            }
        }
    }

    void ProcessBridge::DispatchFrame(const ZeriFrame& frame) {
        switch (frame.type) {

        case MsgType::EXEC_RESULT: {
            m_awaitingResult.store(false);
            m_watchdogCv.notify_one();

            ResultCallback cb;
            {
                std::lock_guard lock(m_callbackMutex);
                cb = std::move(m_resultCallback);
                m_resultCallback = nullptr;
            }
            if (cb) cb(frame);
            break;
        }

        case MsgType::REQ_INPUT: {
            InputRequestCallback handler;
            {
                std::lock_guard lock(m_callbackMutex);
                handler = m_inputHandler;
            }
            if (handler) handler(frame.payload);
            break;
        }

        case MsgType::SYS_EVENT: {
            if (frame.payload.find("\"READY\"") != std::string::npos) {
                m_ready.store(true);
                m_readyCv.notify_one();
            }
            break;
        }

        default:
            break;
        }
    }

    bool ProcessBridge::SendFrame(const ZeriFrame& frame) {
        std::lock_guard lock(m_writeMutex);
        auto encoded = EncodeFrame(frame);
        return m_host->SendData(encoded);
    }

}

/*
Launch sequence:
1. Calls m_host->Start(executable, args) to spawn the sidecar process.
2. Resets the FrameDecoder (clears any stale state from a previous session).
3. Starts two jthreads: ReaderLoop (pipe consumer) and WatchdogLoop (timeout guard).
4. Blocks on m_readyCv.wait_for() until the sidecar sends SYS_EVENT with READY
   or the timeout expires. On timeout, calls Shutdown and returns false.
5. Sets m_active=true only after successful handshake.

ReaderLoop:
  Blocking read loop: calls ReceiveData (which blocks until bytes arrive or pipe
  closes), feeds the bytes to FrameDecoder, then drains any additional complete
  frames by calling Feed({}) in a loop. Feed({}) with empty input processes
  leftover bytes stored in the decoder's m_pending buffer. This handles the case
  where a single pipe read contains multiple complete Zeri-Wire frames.
  The loop exits when ReceiveData returns false (pipe closed) or stop is requested.

WatchdogLoop:
  Two-phase wait pattern using a single condition_variable:
  Phase 1: wait until m_awaitingResult becomes true (activated by ExecuteCode).
  Phase 2: wait_for the configured timeout until m_awaitingResult becomes false
           (cleared by DispatchFrame on EXEC_RESULT) or stop is requested.
  If phase 2 times out with m_awaitingResult still true:
    - Sends SYS_EVENT INTERRUPT to the sidecar (signal to abort execution).
    - Fires the stored ResultCallback with a timeout error payload.
  The mutex is unlocked before calling SendFrame and the callback to avoid
  holding locks during I/O or user code.

DispatchFrame routing:
  EXEC_RESULT → clears watchdog, copies callback out of lock, invokes it.
  REQ_INPUT → copies input handler out of lock, invokes with payload.
  REQ_INPUT → The handler notifies the UI; the UI later calls SendInputResponse.
  SYS_EVENT → checks for READY substring to unblock Launch handshake.
  SYS_EVENT → Other events (CRASHED etc.) are silently ignored in V1.

Shutdown ordering (critical for avoiding deadlock/UAF):
1. Send TERMINATE SYS_EVENT (best-effort, pipe may already be broken).
2. Clear m_awaitingResult and request_stop on watchdog thread, notify its CV.
3. request_stop on reader thread (sets stop token but doesn't unblock read).
4. m_host->Stop() closes pipes, which unblocks ReceiveData → reader exits.
5. Join watchdog then reader. Order matters: watchdog must exit before reader
   because watchdog may call SendFrame which needs the pipe alive briefly.
6. Reset decoder and clear callbacks.

The Shutdown guard uses compare_exchange_strong on m_active. If m_active was
already false (e.g., Launch failed mid-handshake and already called Shutdown),
the else branch still cleans up threads and host without sending TERMINATE.

SendFrame:
  Locks m_writeMutex, encodes the ZeriFrame to wire format, writes to the pipe.
  Returns false if the pipe write fails. All write paths go through this method.

InputRequestCallback design:
  The callback signature is void(const std::string&) — it receives the REQ_INPUT
  payload and notifies the UI asynchronously. The UI then calls SendInputResponse
  with the user's answer. This avoids blocking the reader thread during input,
  which would prevent watchdog cancellation and other frame processing.
*/
