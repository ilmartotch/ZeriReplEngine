#pragma once

#include "ModuleManifest.h"
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>

namespace Zeri::Modules {

    class ModuleManager {
    public:
        ModuleManager();
        ~ModuleManager();

        void StartBackgroundScan();

        [[nodiscard]] std::vector<ModuleManifest> GetModules() const;
        [[nodiscard]] bool IsScanning() const;

    private:
        void ScanTask();
        ModuleManifest ParseManifest(const std::filesystem::path& dirPath);

        std::map<std::string, ModuleManifest> m_modules;
        mutable std::mutex m_mutex;

        std::thread m_scanThread;
        std::atomic<bool> m_isScanning{ false };
        std::atomic<bool> m_stopRequested{ false };
        const std::string m_modulesRoot = "modules";
    };

}

/*
ModuleManager.h — Background-scanned module registry.

Responsabilità:
  - StartBackgroundScan(): Launches a jthread that iterates `modules/`
    directory, parses manifests (JSON or auto-detect) and populates the map.
  - GetModules(): Thread-safe snapshot of discovered modules.
  - IsScanning(): Atomic flag for scan-in-progress status.

Uses std::jthread (C++20) for automatic join on destruction, ensuring
safe thread cleanup. Scanning is decoupled from the main thread for
fast startup times.

Dipendenze: ModuleManifest, filesystem, nlohmann_json (in .cpp).
*/
