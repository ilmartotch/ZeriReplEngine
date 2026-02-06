#pragma once

#include <string>
#include <vector>

namespace Zeri::Modules {

    struct ModuleManifest {
        std::string name;
        std::string version;
        std::string description;
        std::string entryPoint;  // e.g., "main.lua" or "bin/tool.exe"
        std::string type;        // "cpp", "lua", "system"
        std::string path;        // Absolute path to module directory

        [[nodiscard]] bool IsValid() const {
            return !name.empty() && !path.empty();
        }
    };

}
