#pragma once
#include "../../Engine/Include/ReplEngine.h"
#include "../../Engine/Include/InputProcessor.h"

namespace Zeri::Interface {

	class TerminalUi {
	public:
		explicit TerminalUi(Engine::ReplEngine& engine);
		void Run();

	private:
		Engine::ReplEngine& m_engine;
		Engine::InputProcessor m_inputProcessor;

		void ShowPromt() const;
		void ShowWelcomeMessage() const;
		void showContinuationPrompt() const;
		void DisplayOutput(const std::string& output) const;
		void DisplayError(const std::string& error) const;

		[[nodiscard]] bool ReadLine(std::string& buffer) const;
	};
}