#include "../Include/LuaPluginLoader.h"

#include "../../Core/Include/RuntimeState.h"
#include "../../Core/Include/StringUtils.h"

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <system_error>

namespace {

    [[nodiscard]] std::string ToLower(std::string value) {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    [[nodiscard]] bool IsLuaPluginFile(const std::filesystem::path& path) {
        if (!path.has_extension()) {
            return false;
        }
        return ToLower(path.extension().string()) == ".lua";
    }

}

namespace Zeri::Engines::Defaults {

    LuaPluginLoader::LuaPluginLoader(Zeri::Core::RuntimeState& state, Zeri::Ui::ITerminal& terminal)
        : m_state(state)
        , m_terminal(terminal) {
    }

    LuaPluginLoader::~LuaPluginLoader() {
        UnloadAll();
    }

    void LuaPluginLoader::LoadAll(const std::filesystem::path& pluginDir) {
        UnloadAll();
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
            if (!IsLuaPluginFile(filePath)) {
                continue;
            }
            (void)RegisterPluginFile(filePath);
        }
    }

    void LuaPluginLoader::UnloadAll() {
        for (auto& plugin : m_plugins) {
            if (plugin.state != nullptr) {
                lua_close(plugin.state);
                plugin.state = nullptr;
            }
        }
        m_plugins.clear();
        m_loadedPluginInfos.clear();
        m_commands.clear();
    }

    bool LuaPluginLoader::ExecuteCommand(const Zeri::Engines::Command& cmd, Zeri::Ui::ITerminal& terminal) {
        const auto commandIt = m_commands.find(ToLower(cmd.commandName));
        if (commandIt == m_commands.end()) {
            return false;
        }

        const LuaCommandBinding& binding = commandIt->second;
        if (binding.pluginIndex >= m_plugins.size()) {
            return false;
        }

        LuaPluginEntry& plugin = m_plugins[binding.pluginIndex];
        lua_State* luaState = plugin.state;
        if (luaState == nullptr) {
            terminal.WriteError(
                "[ZERI][PLUGIN-004] Lua plugin command '" + cmd.commandName + "' failed: plugin state is not available."
            );
            return true;
        }

        lua_rawgeti(luaState, LUA_REGISTRYINDEX, binding.functionRef);
        lua_newtable(luaState);
        for (std::size_t i = 0; i < cmd.args.size(); ++i) {
            lua_pushlstring(luaState, cmd.args[i].c_str(), cmd.args[i].size());
            lua_seti(luaState, -2, static_cast<lua_Integer>(i + 1));
        }

        if (lua_pcall(luaState, 1, 1, 0) != LUA_OK) {
            const char* errorText = lua_tostring(luaState, -1);
            terminal.WriteError(
                "[ZERI][PLUGIN-004] Lua plugin command '" + cmd.commandName + "' failed: " +
                std::string(errorText == nullptr ? "unknown Lua error" : errorText) + "."
            );
            lua_pop(luaState, 1);
            return true;
        }

        if (!lua_isnil(luaState, -1)) {
            luaL_tolstring(luaState, -1, nullptr);
            const char* outputText = lua_tostring(luaState, -1);
            if (outputText != nullptr && *outputText != '\0') {
                terminal.WriteLine(outputText);
            }
            lua_pop(luaState, 1);
        }
        lua_pop(luaState, 1);
        return true;
    }

    const std::vector<LuaPluginInfo>& LuaPluginLoader::LoadedPlugins() const {
        return m_loadedPluginInfos;
    }

    int LuaPluginLoader::LuaWrite(lua_State* luaState) {
        auto* loader = static_cast<LuaPluginLoader*>(lua_touserdata(luaState, lua_upvalueindex(1)));
        if (loader == nullptr) {
            return 0;
        }
        std::size_t length = 0;
        const char* text = luaL_checklstring(luaState, 1, &length);
        loader->m_terminal.WriteLine(std::string(text, length));
        return 0;
    }

    int LuaPluginLoader::LuaGet(lua_State* luaState) {
        auto* loader = static_cast<LuaPluginLoader*>(lua_touserdata(luaState, lua_upvalueindex(1)));
        if (loader == nullptr) {
            lua_pushnil(luaState);
            return 1;
        }

        std::size_t length = 0;
        const char* keyRaw = luaL_checklstring(luaState, 1, &length);
        const std::string key(keyRaw, length);
        const auto value = loader->m_state.GetShared(key);
        if (!value.has_value()) {
            lua_pushnil(luaState);
            return 1;
        }

        const auto serialized = Zeri::Core::RuntimeState::SerializeAnyValue(*value);
        if (!serialized.has_value()) {
            lua_pushnil(luaState);
            return 1;
        }

        if (serialized->is_string()) {
            const std::string data = serialized->get<std::string>();
            lua_pushlstring(luaState, data.c_str(), data.size());
            return 1;
        }
        if (serialized->is_boolean()) {
            lua_pushboolean(luaState, serialized->get<bool>() ? 1 : 0);
            return 1;
        }
        if (serialized->is_number_float()) {
            lua_pushnumber(luaState, serialized->get<double>());
            return 1;
        }
        if (serialized->is_number_integer()) {
            lua_pushinteger(luaState, serialized->get<lua_Integer>());
            return 1;
        }

        const std::string dump = serialized->dump();
        lua_pushlstring(luaState, dump.c_str(), dump.size());
        return 1;
    }

    int LuaPluginLoader::LuaSet(lua_State* luaState) {
        auto* loader = static_cast<LuaPluginLoader*>(lua_touserdata(luaState, lua_upvalueindex(1)));
        if (loader == nullptr) {
            return 0;
        }

        std::size_t keyLength = 0;
        const char* keyRaw = luaL_checklstring(luaState, 1, &keyLength);
        const std::string key(keyRaw, keyLength);

        const int valueType = lua_type(luaState, 2);
        if (valueType == LUA_TSTRING) {
            std::size_t valueLength = 0;
            const char* valueRaw = lua_tolstring(luaState, 2, &valueLength);
            loader->m_state.SetShared(key, std::string(valueRaw, valueLength));
            return 0;
        }
        if (valueType == LUA_TBOOLEAN) {
            loader->m_state.SetShared(key, lua_toboolean(luaState, 2) != 0);
            return 0;
        }
        if (valueType == LUA_TNUMBER) {
            lua_Number value = lua_tonumber(luaState, 2);
            loader->m_state.SetShared(key, static_cast<double>(value));
            return 0;
        }
        if (valueType == LUA_TNIL) {
            loader->m_state.DeleteShared(key);
            return 0;
        }

        luaL_error(luaState, "zeri.set supports only string, number, bool, or nil values");
        return 0;
    }

    int LuaPluginLoader::LuaIoRead(lua_State* luaState) {
        auto* loader = static_cast<LuaPluginLoader*>(lua_touserdata(luaState, lua_upvalueindex(1)));
        if (loader == nullptr) {
            lua_pushliteral(luaState, "");
            return 1;
        }

        std::string prompt;
        if (lua_gettop(luaState) >= 1 && lua_type(luaState, 1) == LUA_TSTRING) {
            std::size_t promptLength = 0;
            const char* promptRaw = lua_tolstring(luaState, 1, &promptLength);
            prompt.assign(promptRaw, promptLength);
        }

        auto readResult = loader->m_terminal.ReadLine(prompt);
        if (!readResult.has_value()) {
            lua_pushliteral(luaState, "");
            return 1;
        }

        lua_pushlstring(luaState, readResult->c_str(), readResult->size());
        return 1;
    }

    void LuaPluginLoader::RegisterSandboxApi(lua_State* luaState) {
        luaL_requiref(luaState, LUA_STRLIBNAME, luaopen_string, 1);
        lua_pop(luaState, 1);
        luaL_requiref(luaState, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(luaState, 1);
        luaL_requiref(luaState, LUA_MATHLIBNAME, luaopen_math, 1);
        lua_pop(luaState, 1);

        lua_newtable(luaState);
        lua_pushlightuserdata(luaState, this);
        lua_pushcclosure(luaState, &LuaPluginLoader::LuaWrite, 1);
        lua_setfield(luaState, -2, "write");
        lua_pushlightuserdata(luaState, this);
        lua_pushcclosure(luaState, &LuaPluginLoader::LuaGet, 1);
        lua_setfield(luaState, -2, "get");
        lua_pushlightuserdata(luaState, this);
        lua_pushcclosure(luaState, &LuaPluginLoader::LuaSet, 1);
        lua_setfield(luaState, -2, "set");
        lua_setglobal(luaState, "zeri");

        lua_newtable(luaState);
        lua_pushlightuserdata(luaState, this);
        lua_pushcclosure(luaState, &LuaPluginLoader::LuaIoRead, 1);
        lua_setfield(luaState, -2, "read");
        lua_setglobal(luaState, "io");

        lua_pushnil(luaState);
        lua_setglobal(luaState, "os");
        lua_pushnil(luaState);
        lua_setglobal(luaState, "load");
        lua_pushnil(luaState);
        lua_setglobal(luaState, "dofile");
    }

    void LuaPluginLoader::LogLuaLoadError(const std::filesystem::path& filePath, const std::string& details) {
        m_terminal.WriteError(
            "[ZERI][PLUGIN-003] Lua plugin '" + filePath.filename().string() +
            "' failed to load: " + details + ". Skipping."
        );
    }

    bool LuaPluginLoader::RegisterPluginFile(const std::filesystem::path& filePath) {
        lua_State* luaState = luaL_newstate();
        if (luaState == nullptr) {
            LogLuaLoadError(filePath, "cannot allocate lua state");
            return false;
        }

        RegisterSandboxApi(luaState);

        if (luaL_loadfile(luaState, filePath.string().c_str()) != LUA_OK) {
            const char* errorText = lua_tostring(luaState, -1);
            LogLuaLoadError(filePath, errorText == nullptr ? "load error" : errorText);
            lua_close(luaState);
            return false;
        }

        if (lua_pcall(luaState, 0, 1, 0) != LUA_OK) {
            const char* errorText = lua_tostring(luaState, -1);
            LogLuaLoadError(filePath, errorText == nullptr ? "runtime error" : errorText);
            lua_close(luaState);
            return false;
        }

        if (!lua_istable(luaState, -1)) {
            LogLuaLoadError(filePath, "plugin entrypoint must return a table");
            lua_pop(luaState, 1);
            lua_close(luaState);
            return false;
        }

        lua_getfield(luaState, -1, "name");
        if (!lua_isstring(luaState, -1)) {
            LogLuaLoadError(filePath, "missing string field 'name'");
            lua_pop(luaState, 2);
            lua_close(luaState);
            return false;
        }
        const std::string pluginName = lua_tostring(luaState, -1);
        lua_pop(luaState, 1);

        lua_getfield(luaState, -1, "version");
        if (!lua_isstring(luaState, -1)) {
            LogLuaLoadError(filePath, "missing string field 'version'");
            lua_pop(luaState, 2);
            lua_close(luaState);
            return false;
        }
        const std::string pluginVersion = lua_tostring(luaState, -1);
        lua_pop(luaState, 1);

        std::string pluginDescription;
        lua_getfield(luaState, -1, "description");
        if (lua_isstring(luaState, -1)) {
            pluginDescription = lua_tostring(luaState, -1);
        }
        lua_pop(luaState, 1);

        lua_getfield(luaState, -1, "commands");
        if (!lua_istable(luaState, -1)) {
            LogLuaLoadError(filePath, "missing table field 'commands'");
            lua_pop(luaState, 2);
            lua_close(luaState);
            return false;
        }

        LuaPluginEntry entry;
        entry.state = luaState;
        entry.name = pluginName;
        entry.version = pluginVersion;
        entry.description = pluginDescription;
        entry.filePath = filePath;

        const std::size_t pluginIndex = m_plugins.size();
        lua_pushnil(luaState);
        while (lua_next(luaState, -2) != 0) {
            if (lua_type(luaState, -2) == LUA_TSTRING && lua_type(luaState, -1) == LUA_TFUNCTION) {
                const std::string commandName = ToLower(lua_tostring(luaState, -2));
                if (!m_commands.contains(commandName)) {
                    lua_pushvalue(luaState, -1);
                    const int functionRef = luaL_ref(luaState, LUA_REGISTRYINDEX);
                    LuaCommandBinding binding;
                    binding.pluginIndex = pluginIndex;
                    binding.functionRef = functionRef;
                    m_commands.emplace(commandName, binding);
                    entry.commands.push_back(commandName);
                }
            }
            lua_pop(luaState, 1);
        }

        lua_pop(luaState, 2);

        if (entry.commands.empty()) {
            LogLuaLoadError(filePath, "no valid commands registered");
            lua_close(luaState);
            return false;
        }

        LuaPluginInfo info;
        info.name = entry.name;
        info.version = entry.version;
        info.description = entry.description;
        info.commands = entry.commands;
        info.filePath = entry.filePath;
        m_loadedPluginInfos.push_back(std::move(info));
        m_plugins.push_back(std::move(entry));
        return true;
    }

}

/*
LuaPluginLoader.cpp
Implements sandboxed Lua plugin discovery and command execution with one isolated state per file.
Each plugin command is invoked with lua_pcall and failures are reported without crashing the engine.
*/
