#pragma once

#include <string>
#include <vector>

namespace Zeri::Modules {

    struct ModuleManifest {
        std::string name;
        std::string version;
        std::string description;
        std::string entryPoint;
        std::string type;
        std::string path;

        [[nodiscard]] bool IsValid() const {
            return !name.empty() && !path.empty();
        }
    };

}

/*
ModuleManifest.h — Data structure describing a discoverable module.

Fields:
  name: Module name (directory name).
  version: Version string from manifest.
  description: Human-readable description.
  entryPoint: Executable or script entry, e.g. "main.lua" or "bin/tool.exe".
  type: Module type classifier: "native", "lua", "system", "unknown".
  path: Absolute path to the module directory.
IsValid(): Requires non-empty name and path.

Dipendenze: nessuna (POD struct).

QA Changes:
  - Rimossi commenti inline sui campi, documentazione spostata qui.
  - Rinominato tipo "cpp" in "native" per chiarezza (il tipo indica moduli
    compilati, non un linguaggio di scripting REPL).
*/
