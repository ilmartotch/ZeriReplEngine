#include "Core/Include/HelpCatalog.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

namespace {

    [[nodiscard]] std::optional<bool> ParseExpectedLoaded(int argc, char* argv[]) {
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg == "--expect-loaded=true") {
                return true;
            }
            if (arg == "--expect-loaded=false") {
                return false;
            }
        }
        return std::nullopt;
    }

}

int main(int argc, char* argv[]) {
    const auto expected = ParseExpectedLoaded(argc, argv);

    auto& catalog = Zeri::Core::HelpCatalog::Instance();
    const bool loaded = catalog.IsLoaded();

    std::cout << "loaded=" << (loaded ? "true" : "false") << "\n";
    std::cout << "contexts=" << catalog.Contexts().size() << "\n";
    std::cout << "global_commands=" << catalog.CommandsForGroup("global").size() << "\n";

    if (expected.has_value() && expected.value() != loaded) {
        std::cerr << "Expected loaded=" << (expected.value() ? "true" : "false")
                  << " but got " << (loaded ? "true" : "false") << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
HelpCatalogSmoke.cpp
Process-level smoke test for HelpCatalog singleton initialization with explicit loaded-state assertions.
*/
