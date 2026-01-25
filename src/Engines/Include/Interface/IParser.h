#pragma once
#include "../Command.h"
#include <expected>
#include <string>

namespace Zeri::Engines {

    struct ParseError {
        std::string message;
        size_t position;
    };

    class IParser {
    public:
        virtual ~IParser() = default;

        [[nodiscard]] virtual std::expected<Command, ParseError> Parse(const std::string& input) = 0;
    };

}

/*
Interface `IParser`.
Contract for any component capable of transforming a raw input string into a structured `Command` object.
Implementations can vary from simple string splitting to complex grammar parsing (e.g., using ANTLR or custom parsers).
*/
