#pragma once

#include "../../Core/Include/RuntimeState.h"

#include <sstream>
#include <string>
#include <vector>

namespace Zeri::Engines::Utils {

    [[nodiscard]] inline std::string BuildLastBufferKey(const std::string& lang) {
        std::string key;
        key.reserve(lang.size() + 21);
        key.append(lang);
        key.append("::editor::last_buffer");
        return key;
    }

    [[nodiscard]] inline std::string AnyToString(const Zeri::Core::AnyValue& value) {
        if (!value.has_value()) {
            return {};
        }
        if (value.type() == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
        return {};
    }

    [[nodiscard]] inline std::vector<std::string> SplitLines(const std::string& input) {
        std::vector<std::string> lines;
        std::istringstream stream(input);
        std::string line;
        while (std::getline(stream, line)) {
            lines.push_back(std::move(line));
        }
        if (lines.empty()) {
            lines.push_back({});
        }
        return lines;
    }

}
