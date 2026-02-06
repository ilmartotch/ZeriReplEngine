#pragma once
#include "Interface/IParser.h"

namespace Zeri::Engines::Defaults {

    class MetaLanguageParser : public IParser {
    public:
        [[nodiscard]] std::expected<Command, ParseError> Parse(const std::string& input) override;

    private:
        [[nodiscard]] std::string StripComment(const std::string& input, std::string& outComment) const;
        [[nodiscard]] bool TryParseAssignment(const std::string& input, Command& cmd) const;
        [[nodiscard]] bool TryParseFunctionCall(const std::string& input, Command& cmd) const;
        [[nodiscard]] CommandPrefix DetectPrefix(const std::string& input) const;
    };

}

/*
Header for `MetaLanguageParser`.
Implements the full parsing logic for the meta-language specification.
Handles symbol semantics: / (commands), -- (flags), () (functions), = (binding), # (comments).
Enforces the rule: "Only one command is allowed per line" ("Command Model").
*/