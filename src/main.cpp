#include "Engine/Include/ReplEngine.h"
#include "Interface/Include/TerminalUi.h"

int main() {

	// Istance the REPL engine
	Zeri::Engine::ReplEngine engine;

	// Instance the Ui with the engine
	Zeri::Interface::TerminalUi terminalInterface(engine);

	// Start the loop
	terminalInterface.Run();

	return 0;
}