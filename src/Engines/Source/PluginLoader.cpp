#include "../Include/PluginLoader.h"

#include "../../Core/Include/StringUtils.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <nlohmann/json.hpp>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {

#ifdef _WIN32
    using NativeHandle = HMODULE;
#else
    using NativeHandle = void*;
#endif

    [[nodiscard]] std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    [[nodiscard]] std::string NormalizeName(const char* value, std::string fallback) {
        if (value == nullptr || *value == '\0') {
            return fallback;
        }
        return std::string(value);
    }

    [[nodiscard]] std::string ReadHomeEnv(const char* name) {
#if defined(_WIN32)
        char* value = nullptr;
        std::size_t length = 0;
        if (_dupenv_s(&value, &length, name) != 0 || value == nullptr || length <= 1) {
            if (value != nullptr) {
                std::free(value);
            }
            return {};
        }
        std::string out(value);
        std::free(value);
        return out;
#else
        const char* value = std::getenv(name);
        if (value == nullptr || *value == '\0') {
            return {};
        }
        return std::string(value);
#endif
    }

    [[nodiscard]] std::string LoadManifestDescription(
        const std::filesystem::path& pluginFilePath,
        const std::string& fallbackDescription
    ) {
        std::error_code ec;
        const auto manifestPath = pluginFilePath.parent_path() / "plugin.json";
        if (!std::filesystem::exists(manifestPath, ec)) {
            return fallbackDescription;
        }

        std::ifstream input(manifestPath);
        if (!input.is_open()) {
            return fallbackDescription;
        }

        try {
            nlohmann::json manifest = nlohmann::json::parse(input);
            const std::string entry = manifest.value("entry", "");
            if (!entry.empty()) {
                const std::filesystem::path entryPath(entry);
                if (ToLower(entryPath.filename().string()) != ToLower(pluginFilePath.filename().string())) {
                    return fallbackDescription;
                }
            }
            const std::string description = manifest.value("description", "");
            if (description.empty()) {
                return fallbackDescription;
            }
            return description;
        } catch (...) {
            return fallbackDescription;
        }
    }

    [[nodiscard]] NativeHandle OpenLibrary(const std::filesystem::path& path) {
#ifdef _WIN32
        return ::LoadLibraryW(path.wstring().c_str());
#else
        return ::dlopen(path.string().c_str(), RTLD_NOW);
#endif
    }

    void CloseLibrary(NativeHandle handle) {
        if (handle == nullptr) {
            return;
        }
#ifdef _WIN32
        ::FreeLibrary(handle);
#else
        ::dlclose(handle);
#endif
    }

    [[nodiscard]] void* ResolveSymbol(NativeHandle handle, const char* symbolName) {
#ifdef _WIN32
        return reinterpret_cast<void*>(::GetProcAddress(handle, symbolName));
#else
        return ::dlsym(handle, symbolName);
#endif
    }

    [[nodiscard]] bool IsNativePluginFile(const std::filesystem::path& path) {
        if (!path.has_extension()) {
            return false;
        }
        const std::string extension = ToLower(path.extension().string());
#ifdef _WIN32
        return extension == ".dll";
#elif defined(__APPLE__)
        return extension == ".dylib";
#else
        return extension == ".so";
#endif
    }

#ifdef _WIN32
    using ProtectedCall = bool(*)(void*);

    [[nodiscard]] bool InvokeWithProtection(ProtectedCall call, void* userData) {
        __try {
            return call(userData);
        } __except (EXCEPTION_EXECUTE_HANDLER) {
            return false;
        }
    }
#else
    using ProtectedCall = bool(*)(void*);

    [[nodiscard]] bool InvokeWithProtection(ProtectedCall call, void* userData) {
        return call(userData);
    }
#endif

    struct CreateExecutorArgs {
        zeri_create_executor_fn createFn{ nullptr };
        Zeri::Engines::IExecutor* executor{ nullptr };
    };

    [[nodiscard]] bool ProtectedCreateExecutor(void* rawArgs) {
        auto* args = static_cast<CreateExecutorArgs*>(rawArgs);
        if (args == nullptr || args->createFn == nullptr) {
            return true;
        }
        try {
            args->executor = args->createFn();
            return true;
        } catch (...) {
            return false;
        }
    }

    struct CreateContextArgs {
        zeri_create_context_fn createFn{ nullptr };
        Zeri::Core::RuntimeState* state{ nullptr };
        Zeri::Engines::IContext* context{ nullptr };
    };

    [[nodiscard]] bool ProtectedCreateContext(void* rawArgs) {
        auto* args = static_cast<CreateContextArgs*>(rawArgs);
        if (args == nullptr || args->createFn == nullptr || args->state == nullptr) {
            return true;
        }
        try {
            args->context = args->createFn(*args->state);
            return true;
        } catch (...) {
            return false;
        }
    }

    class PluginContextProxy final : public Zeri::Engines::IContext {
    public:
        PluginContextProxy(
            Zeri::Engines::IContext* pluginContext,
            zeri_destroy_context_fn destroyContext
        )
            : m_pluginContext(pluginContext)
            , m_destroyContext(destroyContext) {
        }

        ~PluginContextProxy() override {
            if (m_pluginContext == nullptr) {
                return;
            }
            if (m_destroyContext != nullptr) {
                m_destroyContext(m_pluginContext);
            } else {
                delete m_pluginContext;
            }
            m_pluginContext = nullptr;
        }

        void OnEnter(Zeri::Ui::ITerminal& terminal) override {
            m_pluginContext->OnEnter(terminal);
        }

        void OnExit(Zeri::Ui::ITerminal& terminal) override {
            m_pluginContext->OnExit(terminal);
        }

        [[nodiscard]] std::string GetName() const override {
            return m_pluginContext->GetName();
        }

        [[nodiscard]] std::string GetPrompt() const override {
            return m_pluginContext->GetPrompt();
        }

        [[nodiscard]] Zeri::Engines::ExecutionOutcome HandleCommand(
            const Zeri::Engines::Command& cmd,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override {
            return m_pluginContext->HandleCommand(cmd, state, terminal);
        }

        [[nodiscard]] bool WantsRawInput() const override {
            return m_pluginContext->WantsRawInput();
        }

        [[nodiscard]] Zeri::Engines::ExecutionOutcome HandleRawLine(
            const std::string& line,
            Zeri::Core::RuntimeState& state,
            Zeri::Ui::ITerminal& terminal
        ) override {
            return m_pluginContext->HandleRawLine(line, state, terminal);
        }

        [[nodiscard]] bool IsGlobalCommand(const std::string& name) const override {
            return m_pluginContext->IsGlobalCommand(name);
        }

    private:
        Zeri::Engines::IContext* m_pluginContext{ nullptr };
        zeri_destroy_context_fn m_destroyContext{ nullptr };
    };

}

namespace Zeri::Engines::Defaults {

    struct PluginLoader::NativePlugin {
        NativeHandle handle{ nullptr };
        std::filesystem::path filePath;
        std::string name;
        std::string version;
        std::string description;
        std::string contextName;
        bool hasExecutor{ false };
        bool hasContext{ false };
        zeri_create_executor_fn createExecutor{ nullptr };
        zeri_create_context_fn createContext{ nullptr };
        zeri_destroy_executor_fn destroyExecutor{ nullptr };
        zeri_destroy_context_fn destroyContext{ nullptr };
    };

    PluginLoader::PluginLoader(Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal)
        : m_state(state)
        , m_terminal(terminal) {
    }

    PluginLoader::~PluginLoader() {
        UnloadAll();
    }

    std::filesystem::path PluginLoader::ResolveDefaultPluginDirectory() {
        std::string home = ReadHomeEnv("HOME");
#if defined(_WIN32)
        if (home.empty()) {
            home = ReadHomeEnv("USERPROFILE");
        }
#endif
        if (home.empty()) {
            std::error_code ec;
            return std::filesystem::current_path(ec) / ".zeri" / "plugins";
        }
        return std::filesystem::path(home) / ".zeri" / "plugins";
    }

    void PluginLoader::LoadAll(const std::filesystem::path& pluginDir) {
        UnloadAll();
        m_pluginDirectory = pluginDir;
        std::error_code ec;
        std::filesystem::create_directories(pluginDir, ec);
        ec.clear();
        if (!std::filesystem::exists(pluginDir, ec)) {
            return;
        }

        for (const auto& entry : std::filesystem::directory_iterator(pluginDir, ec)) {
            if (ec) {
                break;
            }
            if (!entry.is_regular_file(ec)) {
                continue;
            }

            const auto& filePath = entry.path();
            if (!IsNativePluginFile(filePath)) {
                continue;
            }

            NativeHandle handle = OpenLibrary(filePath);
            if (handle == nullptr) {
                m_terminal.WriteError(
                    "[ZERI][PLUGIN-002] Plugin '" + filePath.filename().string() +
                    "' initialization failed. Skipping."
                );
                continue;
            }

            auto abiVersion = reinterpret_cast<zeri_plugin_abi_version_fn>(
                ResolveSymbol(handle, "zeri_plugin_abi_version")
            );
            int pluginAbi = -1;
            if (abiVersion != nullptr) {
                try {
                    pluginAbi = abiVersion();
                } catch (...) {
                    pluginAbi = -1;
                }
            }
            if (abiVersion == nullptr || pluginAbi != ZERI_PLUGIN_ABI_VERSION) {
                m_terminal.WriteError(
                    "[ZERI][PLUGIN-001] Plugin '" + filePath.filename().string() +
                    "' ABI version mismatch. Expected " + std::to_string(ZERI_PLUGIN_ABI_VERSION) +
                    ", got " + std::to_string(pluginAbi) + ". Skipping."
                );
                CloseLibrary(handle);
                continue;
            }

            auto pluginNameFn = reinterpret_cast<zeri_plugin_name_fn>(ResolveSymbol(handle, "zeri_plugin_name"));
            auto pluginVersionFn = reinterpret_cast<zeri_plugin_version_fn>(ResolveSymbol(handle, "zeri_plugin_version"));
            auto createExecutorFn = reinterpret_cast<zeri_create_executor_fn>(ResolveSymbol(handle, "zeri_create_executor"));
            auto createContextFn = reinterpret_cast<zeri_create_context_fn>(ResolveSymbol(handle, "zeri_create_context"));
            auto destroyExecutorFn = reinterpret_cast<zeri_destroy_executor_fn>(ResolveSymbol(handle, "zeri_destroy_executor"));
            auto destroyContextFn = reinterpret_cast<zeri_destroy_context_fn>(ResolveSymbol(handle, "zeri_destroy_context"));

            if (pluginNameFn == nullptr || pluginVersionFn == nullptr) {
                m_terminal.WriteError(
                    "[ZERI][PLUGIN-002] Plugin '" + filePath.filename().string() +
                    "' initialization failed. Skipping."
                );
                CloseLibrary(handle);
                continue;
            }

            CreateExecutorArgs executorArgs;
            executorArgs.createFn = createExecutorFn;
            const bool executorCreated = InvokeWithProtection(&ProtectedCreateExecutor, &executorArgs);

            CreateContextArgs contextArgs;
            contextArgs.createFn = createContextFn;
            contextArgs.state = &m_state;
            const bool contextCreated = InvokeWithProtection(&ProtectedCreateContext, &contextArgs);

            if (!executorCreated || !contextCreated) {
                if (executorArgs.executor != nullptr) {
                    if (destroyExecutorFn != nullptr) {
                        destroyExecutorFn(executorArgs.executor);
                    } else {
                        delete executorArgs.executor;
                    }
                }
                if (contextArgs.context != nullptr) {
                    if (destroyContextFn != nullptr) {
                        destroyContextFn(contextArgs.context);
                    } else {
                        delete contextArgs.context;
                    }
                }
                m_terminal.WriteError(
                    "[ZERI][PLUGIN-002] Plugin '" + filePath.filename().string() +
                    "' initialization failed. Skipping."
                );
                CloseLibrary(handle);
                continue;
            }

            const bool hasExecutor = executorArgs.executor != nullptr;
            const bool hasContext = contextArgs.context != nullptr;
            if (!hasExecutor && !hasContext) {
                m_terminal.WriteError(
                    "[ZERI][PLUGIN-002] Plugin '" + filePath.filename().string() +
                    "' initialization failed. Skipping."
                );
                CloseLibrary(handle);
                continue;
            }

            std::string contextName;
            if (contextArgs.context != nullptr) {
                contextName = contextArgs.context->GetName();
            }

            if (executorArgs.executor != nullptr) {
                if (destroyExecutorFn != nullptr) {
                    destroyExecutorFn(executorArgs.executor);
                } else {
                    delete executorArgs.executor;
                }
            }
            if (contextArgs.context != nullptr) {
                if (destroyContextFn != nullptr) {
                    destroyContextFn(contextArgs.context);
                } else {
                    delete contextArgs.context;
                }
            }

            NativePlugin plugin;
            plugin.handle = handle;
            plugin.filePath = filePath;
            plugin.name = NormalizeName(pluginNameFn(), filePath.stem().string());
            plugin.version = NormalizeName(pluginVersionFn(), "0.0.0");
            plugin.description = LoadManifestDescription(filePath, "");
            plugin.contextName = contextName;
            plugin.hasExecutor = hasExecutor;
            plugin.hasContext = hasContext;
            plugin.createExecutor = createExecutorFn;
            plugin.createContext = createContextFn;
            plugin.destroyExecutor = destroyExecutorFn;
            plugin.destroyContext = destroyContextFn;
            m_plugins.push_back(std::move(plugin));

            NativePluginInfo info;
            info.name = m_plugins.back().name;
            info.version = m_plugins.back().version;
            info.description = m_plugins.back().description;
            info.contextName = m_plugins.back().contextName;
            info.hasExecutor = m_plugins.back().hasExecutor;
            info.hasContext = m_plugins.back().hasContext;
            info.filePath = m_plugins.back().filePath;
            m_loadedPluginInfos.push_back(std::move(info));
        }
    }

    void PluginLoader::UnloadAll() {
        for (auto& plugin : m_plugins) {
            CloseLibrary(plugin.handle);
            plugin.handle = nullptr;
        }
        m_plugins.clear();
        m_loadedPluginInfos.clear();
    }

    Zeri::Engines::ContextPtr PluginLoader::CreateContext(const std::string& contextName) {
        const std::string normalized = ToLower(contextName);
        for (auto& plugin : m_plugins) {
            if (!plugin.hasContext || plugin.createContext == nullptr) {
                continue;
            }
            if (ToLower(plugin.contextName) != normalized) {
                continue;
            }

            CreateContextArgs contextArgs;
            contextArgs.createFn = plugin.createContext;
            contextArgs.state = &m_state;
            if (!InvokeWithProtection(&ProtectedCreateContext, &contextArgs) || contextArgs.context == nullptr) {
                m_terminal.WriteError(
                    "[ZERI][PLUGIN-002] Plugin '" + plugin.name + "' initialization failed. Skipping."
                );
                return nullptr;
            }

            return std::make_unique<PluginContextProxy>(contextArgs.context, plugin.destroyContext);
        }
        return nullptr;
    }

    bool PluginLoader::HasContext(const std::string& contextName) const {
        const std::string normalized = ToLower(contextName);
        for (const auto& plugin : m_plugins) {
            if (!plugin.hasContext) {
                continue;
            }
            if (ToLower(plugin.contextName) == normalized) {
                return true;
            }
        }
        return false;
    }

    const std::vector<NativePluginInfo>& PluginLoader::LoadedPlugins() const {
        return m_loadedPluginInfos;
    }

    const std::filesystem::path& PluginLoader::PluginDirectory() const {
        return m_pluginDirectory;
    }

}

/*
PluginLoader.cpp
Implements native plugin loading through the frozen C ABI and platform dynamic loader APIs.
It validates ABI version, probes plugin factories with protected calls, and exposes context creation for runtime dispatch.
*/
