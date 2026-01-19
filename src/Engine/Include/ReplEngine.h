#pragma once
#include <string>
#include <string_view>
#include "ExecutionResult.h"

namespace Zeri::Engine {

	class ReplEngine {
	public:
		[[nodiscard]] ExecutionResult Execute(std::string_view input) noexcept;

		[[nodiscard]] bool ShoulExit() const { return m_shouldExit; }

	private:
		bool m_shouldExit = false;

		[[nodiscard]] ExecutionResult ParseAndExecute(std::string_view input) noexcept;
	};
}