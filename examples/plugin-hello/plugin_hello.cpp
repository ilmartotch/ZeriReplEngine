#include "../../src/Engines/Include/ZeriPluginABI.h"
#include "../../src/Engines/Include/GlobalCommandRegistry.h"
#include "../../src/Core/Include/StringUtils.h"

#include <memory>
#include <string>

namespace {

    class HelloExecutor final : public Zeri::Engines::IExecutor {
    public:
        [[nodiscard]] Zeri::Engines::ExecutionOutcome Execute(
            const Zeri::Engines::Command& cmd,
            Zeri::Core::RuntimeState&,
            Zeri::Ui::ITerminal&
        ) override {
            std::string input = cmd.rawInput;
            if (input.empty()) {
                input = Zeri::Core::Utils::JoinArgs(cmd.args);
            }
            if (input.empty()) {
                input = "world";
            }
            return Zeri::Engines::ExecutionMessage("Hello, " + input + "!");
        }

        [[nodiscard]] Zeri::Engines::ExecutionType GetType() const override {
            return Zeri::Engines::ExecutionType::Unknown;
        }
    };

    class HelloContext final : public Zeri::Engines::IContext {
    public:
        void OnEnter(Zeri::Ui::ITerminal& terminal) override {
            terminal.WriteInfo("Hello context active. Type text and receive a greeting.");
        }

        void OnExit(Zeri::Ui::ITerminal& terminal) override {
            (void)terminal;
        }

        [[nodiscard]] std::string GetName() const override {
            return "hello";
        }

        [[nodiscard]] std::string GetPrompt() const override {
            return "hello";
        }

        [[nodiscard]] Zeri::Engines::ExecutionOutcome HandleCommand(
            const Zeri::Engines::Command& cmd,
            Zeri::Core::RuntimeState&,
            Zeri::Ui::ITerminal&
        ) override {
            std::string input = cmd.rawInput;
            if (input.empty()) {
                input = Zeri::Core::Utils::JoinArgs(cmd.args);
            }
            if (input.empty()) {
                input = "world";
            }
            return Zeri::Engines::ExecutionMessage("Hello, " + input + "!");
        }

        [[nodiscard]] bool IsGlobalCommand(const std::string& name) const override {
            return Zeri::Engines::IsGlobalCommand(name);
        }
    };

}

extern "C" {

    const char* zeri_plugin_name() {
        return "hello-context";
    }

    const char* zeri_plugin_version() {
        return "1.0.0";
    }

    int zeri_plugin_abi_version() {
        return ZERI_PLUGIN_ABI_VERSION;
    }

    Zeri::Engines::IExecutor* zeri_create_executor() {
        return new HelloExecutor();
    }

    Zeri::Engines::IContext* zeri_create_context(Zeri::Core::RuntimeState&) {
        return new HelloContext();
    }

    void zeri_destroy_executor(Zeri::Engines::IExecutor* executor) {
        delete executor;
    }

    void zeri_destroy_context(Zeri::Engines::IContext* context) {
        delete context;
    }

}
