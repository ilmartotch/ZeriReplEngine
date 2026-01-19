#include "../Include/InputProcessor.h"
#include <algorithm>

namespace Zeri::Engine {

	InputProcessor::ProcessedInput InputProcessor::Process(std::string_view line) {

		if (!m_buffer.empty()) {
			m_buffer += "\n";
		}
		m_buffer.append(line);

		if (NeedsMoreLines(m_buffer)) {
			m_isMultiLine = true;
			return { "", false };
		}

		ProcessedInput result{ std::move(m_buffer), true };
		Reset();
		return result;
	}

	void InputProcessor::Reset() noexcept {
		m_buffer.clear();
		m_isMultiLine = false;
	}

	bool InputProcessor::NeedsMoreLines(std::string_view input) const {
		int braceCount = 0;
		int parenCount = 0;
		int bracketCount = 0;

		for (char c : input) {
			switch (c) {
				case '{': ++braceCount; break;
				case '}': --braceCount; break;
				case '(': ++parenCount; break;
				case ')': --parenCount; break;
				case '[': ++bracketCount; break;
				case ']': --bracketCount; break;
			}
		}

		return braceCount > 0 || parenCount > 0 || bracketCount > 0;
	}
}