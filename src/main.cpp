#include "Core/Include/RuntimeState.h"
#include "Engines/Include/SimpleParser.h"
#include "Engines/Include/StandardDispatcher.h"
#include "Engines/Include/BuiltinExecutor.h"
#include "Engines/Include/LuaExecutorStub.h"
#include "Extensions/Include/ExtensionManager.h"
#include "Ui/Include/TerminalUi.h"

#include <memory>

int main() {
    Zeri::Core::RuntimeState runtimeState;
    Zeri::Ui::TerminalUi terminal;
    Zeri::Extensions::ExtensionManager extensionManager;

    auto parser = std::make_unique<Zeri::Engines::Defaults::SimpleParser>();
    auto dispatcher = std::make_unique<Zeri::Engines::Defaults::StandardDispatcher>();

    extensionManager.RegisterExecutor(std::make_unique<Zeri::Engines::Defaults::BuiltinExecutor>());
    extensionManager.RegisterExecutor(std::make_unique<Zeri::Engines::Defaults::LuaExecutorStub>());

    terminal.WriteLine("Zeri REPL v0.1");
    terminal.WriteLine("Type 'help' to start.");

    while (!runtimeState.IsExitRequested()) {
        auto inputOpt = terminal.ReadLine("zeri> ");
        
        if (!inputOpt.has_value()) {
            break;
        }

        std::string input = *inputOpt;
        if (input.empty()) continue;

        auto parseResult = parser->Parse(input);
        if (!parseResult.has_value()) {
            terminal.WriteError("Parse error: " + parseResult.error().message);
            continue;
        }

        auto& cmd = parseResult.value();
        if (cmd.empty()) continue;

        auto type = dispatcher->Classify(cmd);
        auto* executor = extensionManager.GetExecutor(type);

        if (executor) {
            auto outcome = executor->Execute(cmd, runtimeState);
            if (outcome.has_value()) {
                terminal.WriteLine(outcome.value());
            } else {
                terminal.WriteError("[" + outcome.error().code + "] " + outcome.error().message);
            }
        } else {
            terminal.WriteError("No executor found for this command type.");
        }
    }

    terminal.WriteLine("Goodbye.");
    return 0;
}