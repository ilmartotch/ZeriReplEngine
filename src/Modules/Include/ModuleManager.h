#pragma once

#include "ModuleManifest.h"
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>
#include <stop_token>

namespace Zeri::Modules {

    class ModuleManager {
    public:
        ModuleManager();
        ~ModuleManager();

        // Starts the background scanning thread
        void StartBackgroundScan();

        // Thread-safe retrieval of found modules
        [[nodiscard]] std::vector<ModuleManifest> GetModules() const;
        [[nodiscard]] bool IsScanning() const;

    private:
        void ScanTask(std::stop_token stoken);
        ModuleManifest ParseManifest(const std::filesystem::path& dirPath);

        std::map<std::string, ModuleManifest> m_modules;
        mutable std::mutex m_mutex;
        
        std::jthread m_scanThread;
        std::atomic<bool> m_isScanning{ false };
        const std::string m_modulesRoot = "modules";
    };

}

/*
Manages the lifecycle of user modules.
It uses std::jthread (C++20) which automatically joins on destruction, ensuring safe thread cleanup.
The scanning logic is completely decoupled from the main thread to ensure fast startup times.
*/
