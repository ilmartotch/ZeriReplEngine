#include "../Include/ModuleManager.h"
#include "../../Core/Include/AppPaths.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace Zeri::Modules {

    ModuleManager::ModuleManager()
        : ModuleManager(Zeri::Core::ResolveExecutableDir() / ModuleManager::kModulesRoot) {
    }

    ModuleManager::ModuleManager(std::filesystem::path modulesRoot)
        : m_modulesRoot(std::move(modulesRoot)) {
        if (m_modulesRoot.empty()) {
            m_modulesRoot = std::filesystem::path{ ModuleManager::kModulesRoot };
        }

        if (!fs::exists(m_modulesRoot)) {
            fs::create_directory(m_modulesRoot);
        }
    }

    ModuleManager::~ModuleManager() {
        std::lock_guard<std::mutex> lifecycleLock(m_scanControlMutex);
        m_stopRequested.store(true, std::memory_order_release);
        if (m_scanThread.joinable()) {
            m_scanThread.join();
        }
    }

    void ModuleManager::StartBackgroundScan() {
        std::lock_guard<std::mutex> lifecycleLock(m_scanControlMutex);
        if (m_isScanning.load(std::memory_order_acquire)) {
            return;
        }
        if (m_scanThread.joinable()) {
            m_scanThread.join();
        }
        m_stopRequested.store(false, std::memory_order_release);
        m_isScanning.store(true, std::memory_order_release);

        m_scanThread = std::thread([this] {
            ScanTask();
        });
    }

    bool ModuleManager::IsScanning() const {
        return m_isScanning.load(std::memory_order_acquire);
    }

    std::vector<ModuleManifest> ModuleManager::GetModules() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        std::vector<ModuleManifest> result;
        result.reserve(m_modules.size());
        for (const auto& [name, manifest] : m_modules) {
            result.push_back(manifest);
        }
        return result;
    }

    void ModuleManager::ScanTask() {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_modules.clear();
        }

        try {
            for (const auto& entry : fs::directory_iterator(m_modulesRoot)) {
                if (m_stopRequested.load(std::memory_order_acquire)) {
                    break;
                }

                if (entry.is_directory()) {
                    auto manifest = ParseManifest(entry.path());
                    if (manifest.IsValid()) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_modules[manifest.name] = manifest;
                    }
                }
            }
        } catch (const std::exception&) {
        }
        m_isScanning.store(false, std::memory_order_release);
    }

    ModuleManifest ModuleManager::ParseManifest(const fs::path& dirPath) {
        fs::path manifestPath = dirPath / "manifest.json";
        ModuleManifest manifest;
        manifest.path = fs::absolute(dirPath).string();

        if (!fs::exists(manifestPath)) {
            manifest.name = dirPath.filename().string();
            manifest.type = "unknown";
            
            try {
                for (const auto& entry : fs::directory_iterator(dirPath)) {
                    if (entry.is_regular_file()) {
                        auto ext = entry.path().extension().string();
                        if (ext == ".lua") {
                            manifest.type = "lua";
                            manifest.entryPoint = entry.path().filename().string();
                            break;
                        } else if (ext == ".dll" || ext == ".so" || ext == ".exe") {
                            manifest.type = "native";
                            manifest.entryPoint = entry.path().filename().string();
                            break;
                        }
                    }
                }
            } catch (const std::exception&) {
            }
            
            return manifest;
        }

        try {
            std::ifstream file(manifestPath);
            if (!file.is_open()) {
                manifest.name = dirPath.filename().string();
                manifest.type = "unknown";
                return manifest;
            }

            nlohmann::json j = nlohmann::json::parse(file);

            manifest.name = j.value("name", "");
            manifest.version = j.value("version", "");
            manifest.description = j.value("description", "");
            manifest.type = j.value("type", "");
            manifest.entryPoint = j.value("entry_point", "");

        } catch (const nlohmann::json::exception&) {
        }

        if (manifest.name.empty()) manifest.name = dirPath.filename().string();

        return manifest;
    }

}

/*
Uses std::filesystem to iterate over m_modulesRoot configured at construction.
Default construction resolves m_modulesRoot via Zeri::Core::ResolveExecutableDir()
so the modules/ directory is always sibling to the executable, regardless of CWD.
This fixes a cross-drive crash (exit status 1) that occurred when zeri was launched
from a volume different from the install directory.
The ScanTask runs on a separate thread. It locks the m_mutex ONLY when writing to the map,
minimizing contention with the main thread (which reads the map).
Uses nlohmann::json for robust manifest parsing with proper error handling.
*/
