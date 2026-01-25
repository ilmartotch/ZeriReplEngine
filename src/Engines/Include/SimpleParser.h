#pragma once
#include "Interface/IParser.h"

namespace Zeri::Engines::Defaults {

    class SimpleParser : public IParser {
    public:
        [[nodiscard]] std::expected<Command, ParseError> Parse(const std::string& input) override;
    };

}

/*
Header for `SimpleParser`.
A basic implementation of `IParser` that splits input by whitespace.
Supports basic flag parsing (arguments starting with "--").
Suitable for the MVP phase.
*/
