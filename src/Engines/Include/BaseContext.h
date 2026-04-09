#pragma once

#include "Interface/IContext.h"
#include <map>
#include <any>
#include <vector>
#include <algorithm>

namespace Zeri::Engines {

    class BaseContext : public IContext {
    public:
        void OnEnter(Zeri::Ui::ITerminal& terminal) override { (void)terminal; }
        void OnExit(Zeri::Ui::ITerminal& terminal) override { (void)terminal; }

        [[nodiscard]] bool IsGlobalCommand(const std::string& name) const override {
            static const std::vector<std::string> globals = { "exit", "back", "save", "context", "status", "reset" };
            return std::find(globals.begin(), globals.end(), name) != globals.end();
        }

    protected:
        std::map<std::string, std::any> m_localVariables;

        void SetLocalVariable(const std::string& key, const std::any& value) {
            m_localVariables[key] = value;
        }

        [[nodiscard]] std::any GetLocalVariable(const std::string& key) const {
            if (m_localVariables.contains(key)) return m_localVariables.at(key);
            return {};
        }

        [[nodiscard]] bool HasLocalVariable(const std::string& key) const {
            return m_localVariables.contains(key);
        }
    };

}

/*
BaseContext.h — Default base implementation for IContext.

Responsabilità:
  - Provides default no-op OnEnter/OnExit.
  - Implements IsGlobalCommand() with the canonical global command list
    (exit, back, save, context, status, reset).
  - Offers protected local variable storage (m_localVariables) with
    SetLocalVariable, GetLocalVariable, HasLocalVariable helpers.

Dipendenze: IContext.
*/