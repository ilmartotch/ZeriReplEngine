#pragma once
#include <string>
#include <map>
#include <mutex>
#include <any>

namespace Zeri::Core {

    class RuntimeState {
    public:
        RuntimeState() = default;
        ~RuntimeState() = default;

        void SetVariable(const std::string& key, const std::any& value);
        [[nodiscard]] std::any GetVariable(const std::string& key) const;
        [[nodiscard]] bool HasVariable(const std::string& key) const;

        void RequestExit();
        [[nodiscard]] bool IsExitRequested() const;

    private:
        std::map<std::string, std::any> m_variables;
        mutable std::mutex m_mutex;
        bool m_exitRequested{ false };
    };

}

/*
This header defines the `RuntimeState` class, which serves as the central shared state for the application.
It uses `std::any` to store variables of arbitrary types, allowing flexibility for dynamically typed
scripting languages or different internal data structures.
Thread safety for variable access is managed via a `std::mutex`, ensuring that extensions or engines
running in parallel (if supported in future versions) do not corrupt the state.
The exit flag (`m_exitRequested`) provides a clean way to signal the main loop to terminate from anywhere within the system.
*/
