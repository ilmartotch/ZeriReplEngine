#include "../Include/LuaExecutor.h"
#include "../../Core/Include/RuntimeState.h"
#include <lua.hpp>
#include <sstream>

namespace {
    [[nodiscard]] std::string JoinArgs(const std::vector<std::string>& args) {
        std::ostringstream oss;
        for (size_t i = 0; i < args.size(); ++i) {
            if (i > 0) oss << ' ';
            oss << args[i];
        }
        return oss.str();
    }

    [[nodiscard]] std::string AnyToString(const std::any& value) {
        if (!value.has_value()) return {};
        if (value.type() == typeid(std::string)) return std::any_cast<std::string>(value);
        if (value.type() == typeid(const char*)) return std::string(std::any_cast<const char*>(value));
        if (value.type() == typeid(int)) return std::to_string(std::any_cast<int>(value));
        if (value.type() == typeid(double)) return std::to_string(std::any_cast<double>(value));
        if (value.type() == typeid(bool)) return std::any_cast<bool>(value) ? "true" : "false";
        return {};
    }
}

namespace Zeri::Engines::Defaults {

    void LuaExecutor::LuaStateDeleter::operator()(lua_State* state) const noexcept {
        if (state != nullptr) {
            lua_close(state);
        }
    }

    LuaExecutor::LuaExecutor(Zeri::Ui::ITerminal& terminal)
        : m_terminal(terminal) {
        m_luaState.reset(luaL_newstate());
        if (m_luaState) {
            luaL_openlibs(m_luaState.get());
        }
    }

    LuaExecutor::~LuaExecutor() = default;

    int LuaExecutor::LuaPrint(lua_State* L) {
        auto* ctx = static_cast<LuaBindingContext*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (ctx == nullptr || ctx->terminal == nullptr) {
            lua_pushstring(L, "zeri.print binding context missing");
            lua_error(L);
            return 0;
        }

        const char* msg = luaL_checkstring(L, 1);
        ctx->terminal->WriteLine(msg != nullptr ? msg : "");
        return 0;
    }

    int LuaExecutor::LuaSet(lua_State* L) {
        auto* ctx = static_cast<LuaBindingContext*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (ctx == nullptr || ctx->state == nullptr) {
            lua_pushstring(L, "zeri.set binding context missing");
            lua_error(L);
            return 0;
        }

        const char* key = luaL_checkstring(L, 1);
        const char* value = luaL_checkstring(L, 2);

        ctx->state->SetVariable(
            Zeri::Core::RuntimeState::VariableScope::Global,
            key != nullptr ? key : "",
            std::string(value != nullptr ? value : "")
        );

        return 0;
    }

    int LuaExecutor::LuaGet(lua_State* L) {
        auto* ctx = static_cast<LuaBindingContext*>(lua_touserdata(L, lua_upvalueindex(1)));
        if (ctx == nullptr || ctx->state == nullptr) {
            lua_pushstring(L, "zeri.get binding context missing");
            lua_error(L);
            return 0;
        }

        const char* key = luaL_checkstring(L, 1);
        const std::any value = ctx->state->GetVariable(
            Zeri::Core::RuntimeState::VariableScope::Global,
            key != nullptr ? key : ""
        );

        const std::string text = AnyToString(value);
        if (text.empty()) {
            lua_pushnil(L);
        } else {
            lua_pushlstring(L, text.data(), text.size());
        }

        return 1;
    }

    bool LuaExecutor::BindZeriApi(lua_State* L, LuaBindingContext& bindingCtx) const {
        lua_newtable(L);

        lua_pushlightuserdata(L, &bindingCtx);
        lua_pushcclosure(L, &LuaExecutor::LuaPrint, 1);
        lua_setfield(L, -2, "print");

        lua_pushlightuserdata(L, &bindingCtx);
        lua_pushcclosure(L, &LuaExecutor::LuaSet, 1);
        lua_setfield(L, -2, "set");

        lua_pushlightuserdata(L, &bindingCtx);
        lua_pushcclosure(L, &LuaExecutor::LuaGet, 1);
        lua_setfield(L, -2, "get");

        lua_setglobal(L, "zeri");
        return true;
    }

    ExecutionOutcome LuaExecutor::Execute(const Command& cmd, Zeri::Core::RuntimeState& state) {
        if (!m_luaState) {
            return std::unexpected(ExecutionError{ "LUA_INIT_ERR", "Lua state non inizializzato." });
        }

        lua_State* L = m_luaState.get();
        lua_settop(L, 0);

        LuaBindingContext bindingCtx{ &state, &m_terminal };
        if (!BindZeriApi(L, bindingCtx)) {
            return std::unexpected(ExecutionError{ "LUA_BIND_ERR", "Impossibile registrare API zeri.*." });
        }

        std::string script;
        if (!cmd.args.empty()) {
            script = JoinArgs(cmd.args);
        } else if (cmd.pipeInput.has_value()) {
            script = *cmd.pipeInput;
        }

        if (script.empty()) {
            return std::unexpected(ExecutionError{
                "LUA_INPUT_ERR",
                "Nessuno script Lua fornito.",
                cmd.rawInput,
                { "Usa: /lua <script> oppure pipeline con input Lua." }
            });
        }

        const int loadStatus = luaL_loadstring(L, script.c_str());
        if (loadStatus != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            const std::string msg = err != nullptr ? err : "Errore sintassi Lua sconosciuto.";
            lua_pop(L, 1);
            return std::unexpected(ExecutionError{ "LUA_SYNTAX_ERR", msg, cmd.rawInput });
        }

        const int runStatus = lua_pcall(L, 0, LUA_MULTRET, 0);
        if (runStatus != LUA_OK) {
            const char* err = lua_tostring(L, -1);
            const std::string msg = err != nullptr ? err : "Errore runtime Lua sconosciuto.";
            lua_pop(L, 1);
            return std::unexpected(ExecutionError{ "LUA_RUNTIME_ERR", msg, cmd.rawInput });
        }

        if (lua_gettop(L) > 0 && lua_isstring(L, -1)) {
            const char* top = lua_tostring(L, -1);
            return std::string(top != nullptr ? top : "");
        }

        return "Lua script executed.";
    }

    ExecutionType LuaExecutor::GetType() const {
        return ExecutionType::LuaScript;
    }

}