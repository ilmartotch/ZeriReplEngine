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

        void JoinIoThreads();
    };

}

/*
ProcessBridge.h espone solo un'interfaccia C++ portabile, senza includere API native.
I dettagli OS-specifici sono nascosti in un'implementazione privata (PIMPL) definita nel .cpp.
In questo modo i consumer dell'header non dipendono da windows.h né da tipi POSIX.
*/
