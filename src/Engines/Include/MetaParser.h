#pragma once

#include "Interface/IParser.h"
#include <vector>
#include <string>
#include <string_view>
#include <memory_resource>

namespace Zeri::Engines::Defaults {

    /**
     * @brief Advanced parser handling quoted strings and input classification.
     * Replaces the old SimpleParser.
     */
    class MetaParser : public IParser {
    public:
        [[nodiscard]] std::expected<Command, ParseError> Parse(const std::string& input) override;

    private:
        [[nodiscard]] std::pmr::vector<std::pmr::string> Tokenize(std::string_view input, std::pmr::memory_resource* memory);
    };

}

/*
This parser implements a basic Finite State Machine (FSM) in its Tokenize method
to handle arguments enclosed in double quotes (e.g., /echo "Hello World").
It also inspects the first character of the input to determine the InputType
(Command vs ContextSwitch).
*/
