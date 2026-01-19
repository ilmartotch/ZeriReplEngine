#include "../Include/ReplEngine.h"
#include <format>

namespace Zeri::Engine {

	ExecutionResult ReplEngine::Execute(std::string_view input) noexcept {
		try {
			if (input.empty()) {
				return ExecutionResult::Success();
			}

			return ParseAndExecute(input);
		}
		catch (const std::exception& e) {
			return ExecutionResult::Error(
				std::format("Unexpected error: {}", e.what())
			);
		}
		catch (...) {
			return ExecutionResult::Error("Unknown error occurred");
		}
	}

	ExecutionResult ReplEngine::ParseAndExecute(std::string_view input) noexcept {
		if (input == "exit") {
			m_shouldExit = true;
			return ExecutionResult::Exit("Bye see ya KEKW");
		}

		if (input == "help") {
			return ExecutionResult::Success(
				"Available commands:\n"
				"  exit - Exit the REPL\n"
				"  help - Show list of command for the REPL\n"
			);
		}

		return ExecutionResult::Success(
			std::format("Received: {}", input)
		);
	}

}