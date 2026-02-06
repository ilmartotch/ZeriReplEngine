#include "../Include/ModuleManager.h"
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

namespace Zeri::Modules {

    ModuleManager::ModuleManager() {
        // Ensure modules directory exists
        if (!fs::exists(m_modulesRoot)) {
            fs::create_directory(m_modulesRoot);
        }
    }

    ModuleManager::~ModuleManager() {
        // jthread handles join automatically
    }

    void ModuleManager::StartBackgroundScan() {
        if (m_isScanning) return;
        m_isScanning = true;
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

    void ModuleManager::ScanTask() {
        try {
            for (const auto& entry : fs::directory_iterator(m_modulesRoot)) {
                if (entry.is_directory()) {
                    auto manifest = ParseManifest(entry.path());
                    if (manifest.IsValid()) {
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_modules[manifest.name] = manifest;
                    }
                }
            }
        } catch (...) {
            // Log error silently or to a file
        }
        m_isScanning = false;
    }

    // --- Simple Manual JSON Parser (Fallback for MVP without external deps) ---
    // In v0.4, replace this with nlohmann::json
    ModuleManifest ModuleManager::ParseManifest(const fs::path& dirPath) {
        fs::path manifestPath = dirPath / "manifest.json";
        ModuleManifest manifest;
        manifest.path = fs::absolute(dirPath).string();

        if (!fs::exists(manifestPath)) {
            // Auto-detect based on folder name if manifest is missing
            manifest.name = dirPath.filename().string();
            manifest.type = "unknown";
            return manifest;
        }

        std::ifstream file(manifestPath);
        std::string line;
        while (std::getline(file, line)) {
            // Super basic key-value parser: "key": "value"
            auto parseLine = [&](const std::string& key) -> std::string {
                size_t keyPos = line.find("\"" + key + "\"");
                if (keyPos != std::string::npos) {
                    size_t colonPos = line.find(":", keyPos);
                    size_t firstQuote = line.find("\"", colonPos + 1);
                    size_t secondQuote = line.find("\"", firstQuote + 1);
                    if (firstQuote != std::string::npos && secondQuote != std::string::npos) {
                        return line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    }
                }
                return "";
            };

            if (manifest.name.empty()) manifest.name = parseLine("name");
            if (manifest.version.empty()) manifest.version = parseLine("version");
            if (manifest.type.empty()) manifest.type = parseLine("type");
            if (manifest.entryPoint.empty()) manifest.entryPoint = parseLine("entry_point");
        }

        if (manifest.name.empty()) manifest.name = dirPath.filename().string();
        
        return manifest;
    }

} // namespace Zeri::Modules

/*
FILE DOCUMENTATION:
ModuleManager Implementation.
Uses std::filesystem to iterate over the 'modules' directory.
The ScanTask runs on a separate thread. It locks the m_mutex ONLY when writing to the map,
minimizing contention with the main thread (which reads the map).
Includes a rudimentary JSON parser to avoid build dependency hell in this phase.
*/
