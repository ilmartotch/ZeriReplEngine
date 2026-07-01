#pragma once

#include "../../Core/Include/HelpCatalog.h"

#include <string_view>

namespace Zeri::Engines {

    [[nodiscard]] inline bool IsGlobalCommand(std::string_view name) {
        return Zeri::Core::HelpCatalog::Instance().IsEngineGlobalCommand(name);
    }

}
