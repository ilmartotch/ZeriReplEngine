#include "../Include/ModuleManager.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace Zeri::Modules {

    ModuleManager::ModuleManager() {
        // Ensure modules directory exists
        if (!fs::exists(m_modulesRoot)) {
            fs::create_directory(m_modulesRoot);
        }
    }

    ModuleManager::~ModuleManager() {
        if (m_scanThread.joinable()) {
            m_scanThread.request_stop();
        }
    }

    void ModuleManager::StartBackgroundScan() {
        if (m_isScanning) return;
        m_isScanning = true;
        // std::jthread passa automaticamente lo stop_token come primo argomento
        m_scanThread = std::jthread(&ModuleManager::ScanTask, this);
    }

    bool ModuleManager::IsScanning() const {
        return m_isScanning;
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

    void ModuleManager::ScanTask(std::stop_token stoken) {
        try {
            for (const auto& entry : fs::directory_iterator(m_modulesRoot)) {
                if (stoken.stop_requested()) {
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
        } catch (const std::exception& e) {
            // Error log
            std::cerr << "[ModuleManager] Exception during background scan: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[ModuleManager] Unknown exception during background scan.\n";
        }
        m_isScanning = false;
    }

    ModuleManifest ModuleManager::ParseManifest(const fs::path& dirPath) {
        fs::path manifestPath = dirPath / "manifest.json";
        ModuleManifest manifest;
        manifest.path = fs::absolute(dirPath).string();

        if (!fs::exists(manifestPath)) {
            // Auto-detect based on folder name if manifest is missing
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
                            manifest.type = "cpp";
                            manifest.entryPoint = entry.path().filename().string();
                            break;
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "[ModuleManager] Auto-detect failed for " << dirPath << ": " << e.what() << "\n";
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

        } catch (const nlohmann::json::exception& e) {
            std::cerr << "[ModuleManager] Failed to parse manifest: "
                      << manifestPath << " — " << e.what() << "\n";
        }

        if (manifest.name.empty()) manifest.name = dirPath.filename().string();

        return manifest;
    }

}

/*
Uses std::filesystem to iterate over the 'modules' directory.
The ScanTask runs on a separate thread. It locks the m_mutex ONLY when writing to the map,
minimizing contention with the main thread (which reads the map).
Uses nlohmann::json for robust manifest parsing with proper error handling.
*/
