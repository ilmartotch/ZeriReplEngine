#pragma once

#include "Interface/IExecutor.h"
#include "../../Ui/Include/ITerminal.h"
#include <memory>
#include <string>

struct lua_State;

namespace Zeri::Engines::Defaults {

    class LuaExecutor final : public IExecutor {
    public:
        explicit LuaExecutor(Zeri::Ui::ITerminal& terminal);
        ~LuaExecutor() override;

        [[nodiscard]] ExecutionOutcome Execute(
            const Command& cmd,
            Zeri::Core::RuntimeState& state
        ) override;

        [[nodiscard]] ExecutionType GetType() const override;

    private:
        struct LuaStateDeleter {
            void operator()(lua_State* state) const noexcept;
        };

        struct LuaBindingContext {
            Zeri::Core::RuntimeState* state{ nullptr };
            Zeri::Ui::ITerminal* terminal{ nullptr };
        };

        static int LuaPrint(lua_State* L);
        static int LuaSet(lua_State* L);
        static int LuaGet(lua_State* L);

        [[nodiscard]] bool BindZeriApi(lua_State* L, LuaBindingContext& bindingCtx) const;

        std::unique_ptr<lua_State, LuaStateDeleter> m_luaState;
        Zeri::Ui::ITerminal& m_terminal;
    };

}