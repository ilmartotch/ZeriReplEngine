#include "../Include/RuntimeState.h"

namespace Zeri::Core {

    void RuntimeState::SetVariable(const std::string& key, const std::any& value) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_variables[key] = value;
    }

    std::any RuntimeState::GetVariable(const std::string& key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_variables.find(key);
        if (it != m_variables.end()) {
            return it->second;
        }
        return {};
    }

    bool RuntimeState::HasVariable(const std::string& key) const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_variables.contains(key);
    }

    void RuntimeState::RequestExit() {
        m_exitRequested = true;
    }

    bool RuntimeState::IsExitRequested() const {
        return m_exitRequested;
    }

}

/*
Implementation of the RuntimeState methods.
The usage of `std::lock_guard` in variable access methods guarantees that read/write operations
are atomic with respect to the internal map. This prevents data races.
The `GetVariable` method returns an empty `std::any` if the key is not found, forcing the caller
to check validity.
*/
