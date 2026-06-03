#pragma once

#include "Interface/IProcessBridge.h"
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace Zeri::Engines::Defaults {

    class ProcessBridge : public Zeri::Engines::IProcessBridge {
    public:
        ProcessBridge();
        ~ProcessBridge() override;

        [[nodiscard]] ExecutionOutcome Run(
            const std::filesystem::path& executablePath,
            const std::vector<std::string>& args,
            OutputCallback onOutput,
            ErrorCallback onError,
            const std::optional<std::filesystem::path>& cwd = std::nullopt
        ) override;

        [[nodiscard]] int ExecuteSync(
            const std::filesystem::path& executablePath,
            const std::vector<std::string>& args,
            const std::optional<std::filesystem::path>& cwd = std::nullopt
        ) override;

        [[nodiscard]] int WaitForExit() override;

        void SendInput(const std::string& input) override;
        void Terminate() override;
        [[nodiscard]] bool IsRunning() const override { return m_running; }

    private:
        struct Impl;

        void ReadOutputLoop(OutputCallback onOutput);
        void ReadErrorLoop(ErrorCallback onError);

        std::atomic<bool> m_running{ false };
        std::atomic<int> m_lastExitCode{ -1 };
        std::atomic<int> m_activeReadLoops{ 0 };
        std::thread m_outputThread;
        std::thread m_errorThread;
        std::unique_ptr<Impl> m_impl;

        void TerminateImpl();
        void JoinIoThreads();
    };

}

/*
ProcessBridge.h exposes only a portable C++ interface, without native API headers.
OS-specific details are hidden in a private implementation (PIMPL) defined in the .cpp.
This keeps header consumers independent from windows.h and POSIX-specific types.
*/
