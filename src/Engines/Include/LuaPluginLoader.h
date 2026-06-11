#pragma once

#include "Command.h"
#include "../../Core/Include/RuntimeState.h"
#include "../../Ui/Include/ITerminal.h"

#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct lua_State;

namespace Zeri::Engines::Defaults {

    struct LuaPluginInfo {
        std::string name;
        std::string version;
        std::string description;
        std::vector<std::string> commands;
        std::filesystem::path filePath;
    };

    class LuaPluginLoader {
    public:
        LuaPluginLoader(Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal);
        ~LuaPluginLoader();

        void LoadAll(const std::filesystem::path& pluginDir);
        void UnloadAll();

        [[nodiscard]] bool ExecuteCommand(
            const Zeri::Engines::Command& cmd,
            Zeri::Ui::ITerminal& terminal
        );

        [[nodiscard]] const std::vector<LuaPluginInfo>& LoadedPlugins() const;

    private:
        struct LuaCommandBinding {
            std::size_t pluginIndex{ 0 };
            int functionRef{ -1 };
        };

        struct LuaPluginEntry {
            lua_State* state{ nullptr };
            std::string name;
            std::string version;
            std::string description;
            std::filesystem::path filePath;
            std::vector<std::string> commands;
        };

        static int LuaWrite(lua_State* luaState);
        static int LuaGet(lua_State* luaState);
        static int LuaSet(lua_State* luaState);
        static int LuaIoRead(lua_State* luaState);

        [[nodiscard]] bool RegisterPluginFile(const std::filesystem::path& filePath);
        void RegisterSandboxApi(lua_State* luaState);
        void LogLuaLoadError(const std::filesystem::path& filePath, const std::string& details);

        Zeri::Core::RuntimeState& m_state;
        Zeri::Ui::ITerminal& m_terminal;
        std::vector<LuaPluginEntry> m_plugins;
        std::vector<LuaPluginInfo> m_loadedPluginInfos;
        std::map<std::string, LuaCommandBinding> m_commands;
    };

}

/*
LuaPluginLoader.h
Declares a sandboxed Lua plugin loader with isolated Lua states per plugin file.
It exposes command registration and execution through plugin-returned command tables.
*/
