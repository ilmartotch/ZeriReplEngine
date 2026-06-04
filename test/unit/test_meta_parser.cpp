#include "../../src/Engines/Include/MetaParser.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {
    using Zeri::Engines::InputType;
    using Zeri::Engines::Defaults::MetaParser;

    int g_failures = 0;

    void Expect(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "[MetaParser] " << message << "\n";
            ++g_failures;
        }
    }

    void TestSimpleCommand() {
        MetaParser parser;
        const auto result = parser.Parse("/help");
        Expect(result.has_value(), "simple command should parse");
        if (!result.has_value()) {
            return;
        }
        Expect(result->type == InputType::Command, "simple command should be InputType::Command");
        Expect(result->commandName == "help", "command name should be 'help'");
        Expect(result->args.empty(), "simple command should not have args");
        Expect(result->flags.empty(), "simple command should not have flags");
    }

    void TestCommandWithArgs() {
        MetaParser parser;
        const auto result = parser.Parse("/run myscript");
        Expect(result.has_value(), "command with args should parse");
        if (!result.has_value()) {
            return;
        }
        Expect(result->commandName == "run", "command name should be 'run'");
        Expect(result->args.size() == 1, "command with args should have one arg");
        if (!result->args.empty()) {
            Expect(result->args[0] == "myscript", "first arg should be 'myscript'");
        }
    }

    void TestFlagWithValue() {
        MetaParser parser;
        const auto result = parser.Parse("/lua --save myscript");
        Expect(result.has_value(), "flag with value should parse");
        if (!result.has_value()) {
            return;
        }
        const auto flagIt = result->flags.find("save");
        Expect(flagIt != result->flags.end(), "flag 'save' should exist");
        if (flagIt != result->flags.end()) {
            Expect(flagIt->second == "myscript", "flag 'save' value should be 'myscript'");
        }
    }

    void TestBooleanFlag() {
        MetaParser parser;
        const auto result = parser.Parse("/list --all");
        Expect(result.has_value(), "boolean flag should parse");
        if (!result.has_value()) {
            return;
        }
        const auto flagIt = result->flags.find("all");
        Expect(flagIt != result->flags.end(), "flag 'all' should exist");
        if (flagIt != result->flags.end()) {
            Expect(flagIt->second == "true", "flag 'all' should default to 'true'");
        }
    }

    void TestEmptyInput() {
        MetaParser parser;
        const auto result = parser.Parse("");
        if (result.has_value()) {
            Expect(result->type == InputType::Empty, "empty input should produce InputType::Empty");
        } else {
            Expect(true, "empty input can be an error as long as no crash occurs");
        }
    }

    void TestWhitespaceOnlyInput() {
        MetaParser parser;
        const auto result = parser.Parse("   \t   ");
        if (result.has_value()) {
            Expect(result->type == InputType::Empty, "whitespace input should produce InputType::Empty");
        } else {
            Expect(true, "whitespace input can be an error as long as no crash occurs");
        }
    }

    void TestUnicodeInput() {
        MetaParser parser;
        const auto result = parser.Parse("/run café");
        Expect(result.has_value(), "unicode input should parse");
        if (!result.has_value()) {
            return;
        }
        Expect(result->args.size() == 1, "unicode command should have one arg");
        if (!result->args.empty()) {
            Expect(result->args[0] == "café", "unicode arg should preserve UTF-8 content");
        }
    }

    void TestMultipleFlagsAndArgs() {
        MetaParser parser;
        const auto result = parser.Parse("/run --lang lua --save myscript first second");
        Expect(result.has_value(), "multiple flags and args should parse");
        if (!result.has_value()) {
            return;
        }
        Expect(result->commandName == "run", "command name should be 'run'");
        const auto langIt = result->flags.find("lang");
        const auto saveIt = result->flags.find("save");
        Expect(langIt != result->flags.end(), "flag 'lang' should exist");
        Expect(saveIt != result->flags.end(), "flag 'save' should exist");
        if (langIt != result->flags.end()) {
            Expect(langIt->second == "lua", "flag 'lang' value should be 'lua'");
        }
        if (saveIt != result->flags.end()) {
            Expect(saveIt->second == "myscript", "flag 'save' value should be 'myscript'");
        }
        Expect(result->args.size() == 2, "multiple flags input should keep trailing args");
        if (result->args.size() == 2) {
            Expect(result->args[0] == "first", "first trailing arg should be 'first'");
            Expect(result->args[1] == "second", "second trailing arg should be 'second'");
        }
    }
}

int main() {
    TestSimpleCommand();
    TestCommandWithArgs();
    TestFlagWithValue();
    TestBooleanFlag();
    TestEmptyInput();
    TestWhitespaceOnlyInput();
    TestUnicodeInput();
    TestMultipleFlagsAndArgs();

    if (g_failures > 0) {
        std::cerr << "[MetaParser] Failures: " << g_failures << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
