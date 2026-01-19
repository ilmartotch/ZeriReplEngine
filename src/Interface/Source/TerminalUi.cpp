#include "../Include/TerminalUi.h"
#include <iostream>
#include <string>
#include <print>

namespace Zeri::Interface {
	
	TerminalUi::TerminalUi(Engine::ReplEngine& engine)
		: m_engine(engine) {}

	void TerminalUi::Run() {
		ShowWelcomeMessage();

		std::string lineBuffer;

		while (!m_engine.ShoulExit()) {
			if (m_inputProcessor.IsMultiLine()) {
				showContinuationPrompt();
			}
			else {
				ShowPromt();
			}

			if (!ReadLine(lineBuffer)) {
				break;
			}

			auto processed = m_inputProcessor.Process(lineBuffer);
			if (!processed.isComplate) {
				continue;
			}

			auto result = m_engine.Execute(processed.content);

			switch (result.GetStatus()) {
				case Engine::ExecutionStatus::Success:
					if (!result.GetOutput().empty()) {
						DisplayOutput(result.GetOutput());
					}
					break;

				case Engine::ExecutionStatus::Error:
					DisplayError(result.GetError());
					break;

				case Engine::ExecutionStatus::Exit:
					if (!result.GetOutput().empty()) {
						DisplayOutput(result.GetOutput());
					}
					break;
			}
		}
	}

	void TerminalUi::ShowWelcomeMessage() const {
		std::println("Welcome to Zeri");
		std::println("Type 'help' for aveilable commands, type 'exit' to quit\n");
	}

	void TerminalUi::ShowPromt() const {
		std::print("zeri> ");
		std::cout.flush();
	}

	void TerminalUi::showContinuationPrompt() const {
		std::print("... ");
		std::cout.flush();
	}

	void TerminalUi::DisplayOutput(const std::string& output) const {
		std::println("{}", output);
	}

	void TerminalUi::DisplayError(const std::string& error) const {
		std::println("Error: {}", error);
	}

	bool TerminalUi::ReadLine(std::string& buffer) const {
		return static_cast<bool>(std::getline(std::cin, buffer));
	}

}