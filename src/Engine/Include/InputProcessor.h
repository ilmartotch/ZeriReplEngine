#pragma once
#include <string>
#include <string_view>

namespace Zeri::Engine {
	
	class InputProcessor {
	public:
		struct ProcessedInput {
			std::string content;
			bool isComplate;
		};

		[[ndiscard]] ProcessedInput Process(std::string_view line);

		void Reset() noexcept;

		[[nodiscard]] bool IsMultiLine() const noexcept { return m_isMultiLine; }

	private:
		std::string m_buffer;
		bool m_isMultiLine = false;

		[[nodiscard]] bool NeedsMoreLines(std::string_view input) const;
	};
}