#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace Zeri::Core::Utils {

    [[nodiscard]] inline std::string Trim(std::string_view value) {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
            value.remove_suffix(1);
        }
        return std::string(value);
    }

    [[nodiscard]] inline std::string ToLower(std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (char c : value) {
            result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }
        return result;
    }

    [[nodiscard]] inline std::string JoinArgs(const std::vector<std::string>& args, char sep = ' ') {
        std::string result;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) {
                result.push_back(sep);
            }
            result.append(args[i]);
        }
        return result;
    }

    [[nodiscard]] inline std::string JoinTail(const std::vector<std::string>& args, size_t from, char sep = ' ') {
        std::string result;
        for (size_t i = from; i < args.size(); ++i) {
            if (!result.empty()) {
                result.push_back(sep);
            }
            result.append(args[i]);
        }
        return result;
    }

}
