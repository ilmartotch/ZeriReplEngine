#include "../../src/Core/Include/RuntimeState.h"
#include "../../src/Engines/Include/ScriptRegistry.h"

#include <any>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
    using Zeri::Core::RuntimeState;
    using Zeri::Engines::DeleteScript;
    using Zeri::Engines::ListScripts;
    using Zeri::Engines::LoadScript;
    using Zeri::Engines::SaveScript;

    int g_failures = 0;

    void Expect(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "[ScriptRegistry] " << message << "\n";
            ++g_failures;
        }
    }

    std::optional<std::string> ReadEnv(const std::string& name) {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, name.c_str()) != 0 || value == nullptr) {
            return std::nullopt;
        }
        std::string result(value);
        std::free(value);
        return result;
#else
        const char* value = std::getenv(name.c_str());
        if (value == nullptr) {
            return std::nullopt;
        }
        return std::string(value);
#endif
    }

    void SetEnvVar(const std::string& name, const std::string& value) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), value.c_str());
#else
        setenv(name.c_str(), value.c_str(), 1);
#endif
    }

    void UnsetEnvVar(const std::string& name) {
#if defined(_WIN32)
        _putenv_s(name.c_str(), "");
#else
        unsetenv(name.c_str());
#endif
    }

    class ScopedEnv final {
    public:
        ScopedEnv(std::string name, std::string value)
            : m_name(std::move(name))
            , m_previous(ReadEnv(m_name)) {
            SetEnvVar(m_name, value);
        }

        ~ScopedEnv() {
            if (m_previous.has_value()) {
                SetEnvVar(m_name, *m_previous);
            } else {
                UnsetEnvVar(m_name);
            }
        }

    private:
        std::string m_name;
        std::optional<std::string> m_previous;
    };

    std::optional<std::string> AnyToString(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value.type() == typeid(std::string)) {
            return std::any_cast<std::string>(value);
        }
        return std::nullopt;
    }

    class MockRuntimeState final {
    public:
        explicit MockRuntimeState(std::filesystem::path isolationRoot)
            : m_envAppData("APPDATA", isolationRoot.string())
            , m_envUserProfile("USERPROFILE", isolationRoot.string())
            , m_envXdgConfig("XDG_CONFIG_HOME", isolationRoot.string())
            , m_envHome("HOME", isolationRoot.string())
            , m_runtime() {
        }

        RuntimeState& Ref() {
            return m_runtime;
        }

    private:
        ScopedEnv m_envAppData;
        ScopedEnv m_envUserProfile;
        ScopedEnv m_envXdgConfig;
        ScopedEnv m_envHome;
        RuntimeState m_runtime;
    };

    MockRuntimeState CreateIsolatedRuntime() {
        const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        std::filesystem::path isolationRoot = std::filesystem::temp_directory_path() / ("zeri-script-registry-test-" + std::to_string(now));
        std::filesystem::create_directories(isolationRoot);
        return MockRuntimeState(isolationRoot);
    }

    void TestSaveStoresExpectedKey() {
        auto stateHolder = CreateIsolatedRuntime();
        RuntimeState& state = stateHolder.Ref();

        SaveScript(state, "lua", "myscript", "print('ok')");
        const std::string expectedKey = "lua::scripts::myscript";

        Expect(state.HasPersistedVariable(expectedKey), "SaveScript should store {lang}::scripts::{name} key");
        const auto raw = AnyToString(state.GetPersistedVariable(expectedKey));
        Expect(raw.has_value(), "saved script value should be string");
        if (raw.has_value()) {
            Expect(*raw == "print('ok')", "saved script content mismatch");
        }
    }

    void TestLoadScript() {
        auto stateHolder = CreateIsolatedRuntime();
        RuntimeState& state = stateHolder.Ref();

        SaveScript(state, "python", "hello", "print('hello')");
        const auto loaded = LoadScript(state, "python", "hello");
        Expect(loaded.has_value(), "LoadScript should return value for existing key");
        if (loaded.has_value()) {
            Expect(*loaded == "print('hello')", "LoadScript should return saved value");
        }
    }

    void TestLoadMissingReturnsNullopt() {
        auto stateHolder = CreateIsolatedRuntime();
        RuntimeState& state = stateHolder.Ref();

        const auto loaded = LoadScript(state, "ruby", "missing");
        Expect(!loaded.has_value(), "LoadScript should return nullopt for missing key");
    }

    void TestDeleteRemovesFromLoadPath() {
        auto stateHolder = CreateIsolatedRuntime();
        RuntimeState& state = stateHolder.Ref();

        SaveScript(state, "js", "demo", "console.log('demo')");
        const bool deleted = DeleteScript(state, "js", "demo");
        Expect(deleted, "DeleteScript should return true for existing script");
        const auto loaded = LoadScript(state, "js", "demo");
        Expect(!loaded.has_value(), "deleted script should not be loadable");
    }

    void TestListScriptsByLanguage() {
        auto stateHolder = CreateIsolatedRuntime();
        RuntimeState& state = stateHolder.Ref();

        SaveScript(state, "lua", "a", "a()");
        SaveScript(state, "lua", "b", "b()");
        SaveScript(state, "python", "c", "c()");

        const auto luaScripts = ListScripts(state, "lua");
        const auto rubyScripts = ListScripts(state, "ruby");

        Expect(luaScripts.size() == 2, "ListScripts should return all scripts for requested language");
        Expect(rubyScripts.empty(), "ListScripts should return empty list for language without scripts");
    }

    void TestListAfterDeleteExcludesDeleted() {
        auto stateHolder = CreateIsolatedRuntime();
        RuntimeState& state = stateHolder.Ref();

        SaveScript(state, "lua", "one", "one()");
        SaveScript(state, "lua", "two", "two()");
        const bool deleted = DeleteScript(state, "lua", "one");
        Expect(deleted, "DeleteScript should delete existing script");

        const auto list = ListScripts(state, "lua");
        Expect(list.size() == 1, "deleted script should not remain in registry list");
        if (list.size() == 1) {
            Expect(list[0] == "two", "remaining script name mismatch after delete");
        }
    }

    void TestSaveDeleteSaveCycleConsistency() {
        auto stateHolder = CreateIsolatedRuntime();
        RuntimeState& state = stateHolder.Ref();

        SaveScript(state, "lua", "cycle", "v1");
        const bool deletedFirst = DeleteScript(state, "lua", "cycle");
        Expect(deletedFirst, "first delete in cycle should succeed");
        SaveScript(state, "lua", "cycle", "v2");
        const auto loadedAfterResave = LoadScript(state, "lua", "cycle");
        Expect(loadedAfterResave.has_value(), "script should be loadable after re-save");
        if (loadedAfterResave.has_value()) {
            Expect(*loadedAfterResave == "v2", "re-saved script should expose latest content");
        }
        const bool deletedSecond = DeleteScript(state, "lua", "cycle");
        Expect(deletedSecond, "second delete in cycle should succeed");
        SaveScript(state, "lua", "cycle", "v3");
        const auto loadedFinal = LoadScript(state, "lua", "cycle");
        Expect(loadedFinal.has_value(), "script should remain consistent across save/delete/save cycles");
        if (loadedFinal.has_value()) {
            Expect(*loadedFinal == "v3", "final cycle value mismatch");
        }
    }
}

int main() {
    TestSaveStoresExpectedKey();
    TestLoadScript();
    TestLoadMissingReturnsNullopt();
    TestDeleteRemovesFromLoadPath();
    TestListScriptsByLanguage();
    TestListAfterDeleteExcludesDeleted();
    TestSaveDeleteSaveCycleConsistency();

    if (g_failures > 0) {
        std::cerr << "[ScriptRegistry] Failures: " << g_failures << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
