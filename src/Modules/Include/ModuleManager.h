#pragma once

#include "ModuleManifest.h"
#include <vector>
#include <map>
#include <mutex>
#include <thread>
#include <atomic>
#include <filesystem>
#include <string_view>

namespace Zeri::Modules {

    class ModuleManager {
    public:
        ModuleManager();
        explicit ModuleManager(std::filesystem::path modulesRoot);
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
        static constexpr std::string_view kModulesRoot = "modules";
        std::filesystem::path m_modulesRoot;
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

Configurazione root moduli:
  - kModulesRoot rimane il default compile-time condiviso.
  - È disponibile un costruttore esplicito con std::filesystem::path per
    iniettare una root differente per ambienti custom o test.
*/
