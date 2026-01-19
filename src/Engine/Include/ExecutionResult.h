#pragma once
#include <string>
#include <optional>
#include <variant>

namespace Zeri::Engine {
	
	enum class ExecutionStatus {
		Success,
		Error,
		Exit
	};

	class ExecutionResult {
	public:
		static ExecutionResult Success(std::string output = "") {
			ExecutionResult result;
			result.m_status = ExecutionStatus::Success;
			result.m_output = std::move(output);
			return result;
		}

		static ExecutionResult Error(std::string errorMassage) {
			ExecutionResult result;
			result.m_status = ExecutionStatus::Error;
			result.m_errorMessage = std::move(errorMassage);
			return result;
		}

		static ExecutionResult Exit(std::string farewell = "") {
			ExecutionResult result;
			result.m_status = ExecutionStatus::Exit;
			result.m_output = std::move(farewell);
			return result;
		}

		[[nodiscard]] ExecutionStatus GetStatus() const noexcept { return m_status; }
		[[nodiscard]] bool IsSuccess() const noexcept { return m_status == ExecutionStatus::Success; }
		[[nodiscard]] bool IsError() const noexcept { return m_status == ExecutionStatus::Error; }
		[[nodiscard]] bool IsEXit() const noexcept { return m_status == ExecutionStatus::Exit; }

		[[nodiscard]] const std::string& GetOutput() const noexcept { return m_output; }
		[[nodiscard]] const std::string& GetError() const noexcept { return m_errorMessage; }

	private:
		ExecutionResult() = default;

		ExecutionStatus m_status;
		std::string m_output;
		std::string m_errorMessage;
	};
}