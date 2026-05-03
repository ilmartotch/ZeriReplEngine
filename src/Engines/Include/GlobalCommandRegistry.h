#pragma once

#include <array>
#include <algorithm>
#include <string_view>

namespace Zeri::Engines {

    inline constexpr std::array kGlobalCommands = {
        std::string_view{ "exit" },
        std::string_view{ "back" },
        std::string_view{ "save" },
        std::string_view{ "context" },
        std::string_view{ "status" },
        std::string_view{ "reset" }
    };

    [[nodiscard]] inline bool IsGlobalCommand(std::string_view name) {
        return std::ranges::find(kGlobalCommands, name) != kGlobalCommands.end();
    }

}

/*
GlobalCommandRegistry.h — Shared source of truth for global command names.

Responsabilità:
  - Defines kGlobalCommands as a constexpr list used across engine layers.
  - Exposes IsGlobalCommand(std::string_view) for unified command classification.

Dipendenze: standard library only.
*/
