#include "Core/Include/HelpCatalog.h"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
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

    [[nodiscard]] std::optional<std::string> ParseExpectedErrorContains(int argc, char* argv[]) {
        static constexpr std::string_view prefix = "--expect-error-contains=";
        for (int i = 1; i < argc; ++i) {
            const std::string_view arg = argv[i];
            if (arg.starts_with(prefix)) {
                return std::string(arg.substr(prefix.size()));
            }
        }
        return std::nullopt;
    }

}

int main(int argc, char* argv[]) {
    const auto expected = ParseExpectedLoaded(argc, argv);
    const auto expectedErrorContains = ParseExpectedErrorContains(argc, argv);

    auto& catalog = Zeri::Core::HelpCatalog::Instance();
    const bool loaded = catalog.IsLoaded();
    const std::string& error = catalog.LastError();

    std::cout << "loaded=" << (loaded ? "true" : "false") << "\n";
    std::cout << "contexts=" << catalog.Contexts().size() << "\n";
    std::cout << "global_commands=" << catalog.CommandsForGroup("global").size() << "\n";
    std::cout << "engine_global_help=" << (catalog.IsEngineGlobalCommand("help") ? "true" : "false") << "\n";
    std::cout << "last_error=" << error << "\n";

    if (expected.has_value() && expected.value() != loaded) {
        std::cerr << "Expected loaded=" << (expected.value() ? "true" : "false")
                  << " but got " << (loaded ? "true" : "false") << "\n";
        return EXIT_FAILURE;
    }

    if (expectedErrorContains.has_value()) {
        if (error.find(*expectedErrorContains) == std::string::npos) {
            std::cerr << "Expected last_error to contain '" << *expectedErrorContains
                      << "' but got '" << error << "'\n";
            return EXIT_FAILURE;
        }
    }

    if (loaded && !catalog.IsEngineGlobalCommand("help")) {
        std::cerr << "Expected /help to be resolved as engine-global from the embedded commands catalog.\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/*
HelpCatalogSmoke.cpp
Process-level smoke test for HelpCatalog singleton initialization with explicit loaded-state assertions.
*/
