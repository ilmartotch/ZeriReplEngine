#pragma once

#include "Interface/IContext.h"
#include "../../Core/Include/RuntimeState.h"
#include "../../Ui/Include/ITerminal.h"
#include "ZeriPluginABI.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace Zeri::Engines::Defaults {

    struct NativePluginInfo {
        std::string name;
        std::string version;
        std::string description;
        std::string contextName;
        bool hasExecutor{ false };
        bool hasContext{ false };
        std::filesystem::path filePath;
    };

    class PluginLoader {
    public:
        PluginLoader(Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);
        ~PluginLoader();

        void LoadAll(const std::filesystem::path& pluginDir);
        void UnloadAll();

        [[nodiscard]] Zeri::Engines::ContextPtr CreateContext(const std::string& contextName);
        [[nodiscard]] bool HasContext(const std::string& contextName) const;
        [[nodiscard]] const std::vector<NativePluginInfo>& LoadedPlugins() const;
        [[nodiscard]] const std::filesystem::path& PluginDirectory() const;

        [[nodiscard]] static std::filesystem::path ResolveDefaultPluginDirectory();

    private:
        struct NativePlugin;

        Zeri::Core::RuntimeState& m_state;
        Zeri::Ui::ITerminal& m_terminal;
        std::filesystem::path m_pluginDirectory;
        std::vector<NativePlugin> m_plugins;
        std::vector<NativePluginInfo> m_loadedPluginInfos;
    };

}

/*
PluginLoader.h
Declares the native shared-library plugin loader based on the frozen C ABI.
It manages plugin discovery, ABI validation, plugin lifecycle, and context creation.
*/
