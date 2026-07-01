#include "Ui/Include/BridgeProtocol.h"

#include "Core/Include/CatalogRegistry.h"

namespace Zeri::Ui {

    std::string_view BridgeTypeValue(std::string_view id) {
        if (const auto* bridgeType = Zeri::Core::CatalogRegistry::Instance().FindBridgeType(id); bridgeType != nullptr) {
            return bridgeType->value;
        }
        return {};
    }

}
