#include "../Include/CompletionEngine.h"
#include <algorithm>
#include <array>
#include <string_view>
#include <cctype>

namespace Zeri::Engines {

    CompletionEngine::CompletionEngine() = default;

    static bool StartsWith(std::string_view str, std::string_view prefix) {
        if (prefix.empty()) return true;
        if (str.size() < prefix.size()) return false;
        return std::equal(prefix.begin(), prefix.end(), str.begin(), [](char a, char b) {
            return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
        });
    }

    static bool FuzzyMatch(std::string_view str, std::string_view pattern) {
        if (pattern.empty()) return true;
        auto it = std::search(
            str.begin(), str.end(),
            pattern.begin(), pattern.end(),
            [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
            }
        );
        return it != str.end();
    }

    std::vector<Completion> CompletionEngine::GetCompletions(std::string_view input, ReplContext context) const {
        std::vector<Completion> result;

        if (input.empty()) {
            result.push_back({"/", "Global Commands"});
            result.push_back({"$", "Context Switching"});
            result.push_back({"!", "System Commands"});
            return result;
        }

        char trigger = input.front();
        std::string_view query = input;

        if (trigger == '$') {
            for (const auto& ctx : kContexts) {
                if (FuzzyMatch(ctx, query)) {
                    result.push_back({std::string(ctx), "Switch context"});
                }
            }
        } else if (trigger == '/') {
            for (const auto& cmd : kCommands) {
                if (cmd.context == ReplContext::Global || cmd.context == context) {
                    if (FuzzyMatch(cmd.command, query)) {
                        result.push_back({std::string(cmd.command), std::string(cmd.usageHint)});
                    }
                }
            }
        } else {
            for (const auto& cmd : kCommands) {
                if ((cmd.context == ReplContext::Global || cmd.context == context) && FuzzyMatch(cmd.command, input)) {
                    result.push_back({std::string(cmd.command), std::string(cmd.usageHint)});
                }
            }
        }

        return result;
    }

    std::optional<std::string_view> CompletionEngine::GetHint(std::string_view input, ReplContext context) const {
        if (input.empty()) return std::nullopt;

        char trigger = input.front();
        if (trigger == '$') {
            for (const auto& ctx : kContexts) {
                if (ctx == input) return " [Context Switch]";
                if (StartsWith(ctx, input)) return ctx.substr(input.size());
            }
        } else if (trigger == '/') {
            for (const auto& cmd : kCommands) {
                if (cmd.command == input) return cmd.usageHint;
                if ((cmd.context == ReplContext::Global || cmd.context == context) && StartsWith(cmd.command, input)) {
                    return cmd.command.substr(input.size());
                }
            }
        }

        return std::nullopt;
    }

}

/*
CompletionEngine.cpp — Standalone completion and hint generation.

GetCompletions():
  Returns context-filtered command suggestions based on prefix ($, /, text).
  Uses FuzzyMatch for substring search.

GetHint():
  Returns the first matching suffix for inline hint display.

Dipendenze: CompletionEngine.h (kCommands, kContexts arrays).
*/

