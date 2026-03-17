#pragma once

#include "IProcessHost.h"
#include "ZeriWireProtocol.h"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

namespace Zeri::Link {

    class ProcessBridge {
    public:
        using ResultCallback = std::function<void(const ZeriFrame&)>;
        using InputRequestCallback = std::function<void(const std::string&)>;

        explicit ProcessBridge(std::unique_ptr<IProcessHost> host);
        ~ProcessBridge();

        ProcessBridge(const ProcessBridge&) = delete;
        ProcessBridge& operator=(const ProcessBridge&) = delete;
        ProcessBridge(ProcessBridge&&) = delete;
        ProcessBridge& operator=(ProcessBridge&&) = delete;

        [[nodiscard]] bool Launch(
            const std::string& executable,
            const std::vector<std::string>& args,
            std::chrono::seconds timeout = std::chrono::seconds{ 5 }
        );

        void ExecuteCode(const std::string& jsonPayload, ResultCallback callback);
        void SendInputResponse(const std::string& jsonPayload);
        void Shutdown();

        void SetInputRequestHandler(InputRequestCallback handler);
        void SetWatchdogTimeout(std::chrono::seconds timeout);

    private:
        void ReaderLoop(std::stop_token token);
        void WatchdogLoop(std::stop_token token);
        void DispatchFrame(const ZeriFrame& frame);
        bool SendFrame(const ZeriFrame& frame);

        std::unique_ptr<IProcessHost> m_host;
        FrameDecoder m_decoder;
        std::atomic<bool> m_active{ false };

        std::jthread m_readerThread;
        std::jthread m_watchdogThread;

        std::mutex m_writeMutex;

        std::mutex m_callbackMutex;
        ResultCallback m_resultCallback;
        InputRequestCallback m_inputHandler;

        std::mutex m_readyMutex;
        std::condition_variable m_readyCv;
        std::atomic<bool> m_ready{ false };

        std::mutex m_watchdogMutex;
        std::condition_variable m_watchdogCv;
        std::atomic<bool> m_awaitingResult{ false };
        std::chrono::seconds m_watchdogTimeout{ 30 };
    };

}

/*
ProcessBridge owns an IProcessHost (dependency injection) and a FrameDecoder,
coordinating the full lifecycle of a sidecar process: launch with handshake,
bidirectional frame exchange, watchdog timeout, and clean shutdown.

Public API:
- Launch(executable, args, timeout): starts the sidecar, spawns reader and
  watchdog threads, then blocks until SYS_EVENT READY or timeout.
- ExecuteCode(jsonPayload, callback): sends EXEC_CODE, stores callback for
  EXEC_RESULT, activates the watchdog timer.
- SendInputResponse(jsonPayload): sends RES_INPUT in response to a REQ_INPUT
  from the sidecar (user provided input asynchronously).
- Shutdown(): sends TERMINATE SYS_EVENT, stops threads, closes the host.
- SetInputRequestHandler: registers the callback invoked when REQ_INPUT arrives.
- SetWatchdogTimeout: configures execution timeout (default 30s).

Threading model:
- m_readerThread (jthread): blocking read loop on the stdout pipe,
  feeds bytes to FrameDecoder, dispatches complete frames.
- m_watchdogThread (jthread): sleeps until ExecuteCode activates it,
  then waits for EXEC_RESULT or timeout.
- m_writeMutex: serializes all writes to the stdin pipe (SendFrame).
- m_callbackMutex: protects m_resultCallback and m_inputHandler access.
- Callbacks are copied out of the lock before invocation to prevent deadlock.

Lifecycle:
  Constructor → Launch → [ExecuteCode / SendInputResponse]* → Shutdown → Destructor
  m_active atomic guards against double-launch or use-after-shutdown.
*/
