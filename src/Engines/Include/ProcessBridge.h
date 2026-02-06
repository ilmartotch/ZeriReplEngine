#pragma once

#include "Interface/IProcessBridge.h"
#include <atomic>
#include <thread>
#include <vector>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <windows.h>
#else
    #include <sys/types.h>
    #include <unistd.h>
#endif

namespace Zeri::Engines::Defaults {

    class ProcessBridge : public IProcessBridge {
    public:
        ProcessBridge();
        ~ProcessBridge() override;

        [[nodiscard]] ExecutionOutcome Run(
            const std::string& executablePath,
            const std::vector<std::string>& args,
            OutputCallback onOutput
        ) override;

        void SendInput(const std::string& input) override;
        void Terminate() override;
        [[nodiscard]] bool IsRunning() const override { return m_running; }

    private:
        void ReadOutputLoop(OutputCallback onOutput);

        std::atomic<bool> m_running{ false };
        std::jthread m_outputThread;

#ifdef _WIN32
        HANDLE m_hChildProcess{ nullptr };
        HANDLE m_hStdInWrite{ nullptr };
        HANDLE m_hStdOutRead{ nullptr };
#else
        int m_childPid{ -1 };
        int m_stdinPipe[2]{ -1, -1 };
        int m_stdoutPipe[2]{ -1, -1 };
#endif
    };

}

/*
FILE DOCUMENTATION:
ProcessBridge Header.
Uses platform-specific handles (HANDLE for Windows, file descriptors for POSIX).
The m_running atomic flag and m_outputThread (jthread) ensure safe asynchronous output capture.
*/
