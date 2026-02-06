#pragma once

#include "Interface/IParser.h"
#include <vector>
#include <string>

namespace Zeri::Engines::Defaults {

    /**
     * @brief Advanced parser handling quoted strings and input classification.
     * Replaces the old SimpleParser.
     */
    class MetaParser : public IParser {
    public:
        [[nodiscard]] std::expected<Command, ParseError> Parse(const std::string& input) override;

    private:
        [[nodiscard]] std::vector<std::string> Tokenize(const std::string& input);
    };

}

/*
FILE DOCUMENTATION:
MetaParser Header.
This parser implements a basic Finite State Machine (FSM) in its Tokenize method
to handle arguments enclosed in double quotes (e.g., /echo "Hello World").
It also inspects the first character of the input to determine the InputType
(Command vs ContextSwitch).
*/
