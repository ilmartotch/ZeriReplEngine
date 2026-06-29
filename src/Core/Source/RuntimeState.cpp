#include "RuntimeState.h"
#include "../Include/UserPaths.h"
#include "../../Engines/Include/Interface/IContext.h"
#include "../../Modules/Include/ModuleManager.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cctype>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <stdexcept>
#include <thread>
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4702)
#endif
#include <exprtk.hpp>
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(_WIN32)
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace {

    using Zeri::Core::RuntimeState;
    using OverwritePolicy = RuntimeState::OverwritePolicy;
    constexpr int kRuntimeStateSchemaVersion = 2;

    struct CompiledMathFunction {
        exprtk::symbol_table<double> symbolTable;
        exprtk::expression<double> expression;
        std::vector<double> paramStorage;
    };

    [[nodiscard]] bool PersistDurableFile(const std::filesystem::path& path, const std::string& data, std::string& error) {
#if defined(_WIN32)
        const std::wstring nativePath = path.wstring();
        HANDLE handle = CreateFileW(
            nativePath.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr
        );
        if (handle == INVALID_HANDLE_VALUE) {
            error = "Failed to open file for durable write.";
            return false;
        }

        DWORD bytesWritten = 0;
        const DWORD bytesToWrite = static_cast<DWORD>(data.size());
        BOOL writeOk = TRUE;
        if (bytesToWrite > 0U) {
            writeOk = WriteFile(handle, data.data(), bytesToWrite, &bytesWritten, nullptr);
        }
        if (!writeOk || bytesWritten != bytesToWrite) {
            CloseHandle(handle);
            error = "Failed to write serialized session to temp file.";
            return false;
        }

        if (!FlushFileBuffers(handle)) {
            CloseHandle(handle);
            error = "Failed to flush temp session file to disk.";
            return false;
        }

        CloseHandle(handle);
        return true;
#else
        const int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            error = std::string("Failed to open file for durable write: ") + std::strerror(errno);
            return false;
        }

        std::size_t totalWritten = 0;
        while (totalWritten < data.size()) {
            const ssize_t written = write(fd, data.data() + totalWritten, data.size() - totalWritten);
            if (written < 0) {
                close(fd);
                error = std::string("Failed to write serialized session to temp file: ") + std::strerror(errno);
                return false;
            }
            totalWritten += static_cast<std::size_t>(written);
        }

        if (fsync(fd) != 0) {
            close(fd);
            error = std::string("Failed to flush temp session file to disk: ") + std::strerror(errno);
            return false;
        }

        close(fd);
        return true;
#endif
    }

    inline void ApplySavePauseHook() {
        int pauseMs = 0;
#if defined(_WIN32)
        char* pauseRaw = nullptr;
        size_t pauseLen = 0;
        if (_dupenv_s(&pauseRaw, &pauseLen, "ZERI_TEST_SAVE_PAUSE_MS") != 0 || pauseRaw == nullptr || pauseLen == 0) {
            if (pauseRaw != nullptr) {
                std::free(pauseRaw);
            }
            return;
        }
        pauseMs = std::atoi(pauseRaw);
        std::free(pauseRaw);
#else
        const char* pauseRaw = std::getenv("ZERI_TEST_SAVE_PAUSE_MS");
        if (pauseRaw == nullptr || *pauseRaw == '\0') {
            return;
        }
        pauseMs = std::atoi(pauseRaw);
#endif
        if (pauseMs <= 0) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(pauseMs));
    }

    [[nodiscard]] std::expected<RuntimeState::FunctionSignature, std::string> CompileMathFunction(
        const RuntimeState::MathFunctionDefinition& definition
    ) {
        auto compiled = std::make_shared<CompiledMathFunction>();
        compiled->paramStorage.resize(definition.params.size(), 0.0);

        compiled->symbolTable.add_constants();
        compiled->symbolTable.add_constant("euler", 2.718281828459045);
        compiled->symbolTable.add_constant("phi", 1.618033988749895);
        compiled->symbolTable.add_constant("tau", 6.283185307179586);
        compiled->symbolTable.add_constant("sqrt2", 1.4142135623730951);

        for (std::size_t i = 0; i < definition.params.size(); ++i) {
            compiled->symbolTable.add_variable(definition.params[i], compiled->paramStorage[i]);
        }
        compiled->expression.register_symbol_table(compiled->symbolTable);

        exprtk::parser<double> exprParser;
        if (!exprParser.compile(definition.expression, compiled->expression)) {
            std::string diagnostics;
            for (std::size_t i = 0; i < exprParser.error_count(); ++i) {
                if (!diagnostics.empty()) {
                    diagnostics += " | ";
                }
                diagnostics += exprParser.get_error(i).diagnostic;
            }
            if (diagnostics.empty()) {
                diagnostics = "unknown compile error";
            }
            return std::unexpected(diagnostics);
        }

        const auto paramCount = definition.params.size();
        RuntimeState::FunctionSignature fn =
            [compiled, paramCount](const std::vector<double>& args) -> double {
            if (args.size() < paramCount) {
                return 0.0;
            }
            for (std::size_t i = 0; i < paramCount; ++i) {
                compiled->paramStorage[i] = args[i];
            }
            return compiled->expression.value();
            };
        return fn;
    }

    [[nodiscard]] bool MergeAnyValue(std::any& current, const std::any& incoming) {
        if (current.type() == typeid(std::vector<std::any>) &&
            incoming.type() == typeid(std::vector<std::any>)) {
            auto& currentVec = std::any_cast<std::vector<std::any>&>(current);
            const auto& incomingVec = std::any_cast<const std::vector<std::any>&>(incoming);
            currentVec.insert(currentVec.end(), incomingVec.begin(), incomingVec.end());
            return true;
        }

        if (current.type() == typeid(std::map<std::string, std::any>) &&
            incoming.type() == typeid(std::map<std::string, std::any>)) {
            auto& currentMap = std::any_cast<std::map<std::string, std::any>&>(current);
            const auto& incomingMap = std::any_cast<const std::map<std::string, std::any>&>(incoming);
            for (const auto& [key, value] : incomingMap) {
                if (!currentMap.contains(key)) {
                    currentMap.emplace(key, value);
                }
            }
            return true;
        }

        return false;
    }

    [[nodiscard]] std::expected<std::map<std::string, Zeri::Core::AnyValue>, std::string> ParseAnyMap(
        const nlohmann::json& node,
        std::string_view fieldName
    ) {
        if (!node.is_object()) {
            return std::unexpected("Field '" + std::string(fieldName) + "' must be a JSON object.");
        }

        std::map<std::string, Zeri::Core::AnyValue> values;
        for (const auto& [key, valueNode] : node.items()) {
            const auto parsedValue = RuntimeState::DeserializeAnyValue(valueNode);
            if (!parsedValue.has_value()) {
                return std::unexpected("Field '" + std::string(fieldName) + "' has an unsupported value for key '" + key + "'.");
            }
            values[key] = *parsedValue;
        }
        return values;
    }

    [[nodiscard]] std::expected<std::map<std::string, Zeri::Core::AnyValue>, std::string> ParseMathVariables(
        const nlohmann::json& node
    ) {
        if (!node.is_array()) {
            return std::unexpected("Field 'math_state.variables' must be a JSON array.");
        }

        std::map<std::string, Zeri::Core::AnyValue> values;
        for (const auto& item : node) {
            if (!item.is_object()) {
                return std::unexpected("Each math variable entry must be a JSON object.");
            }
            const std::string name = item.value("name", "");
            const std::string type = item.value("type", "");
            if (name.empty()) {
                return std::unexpected("Math variable entry is missing 'name'.");
            }
            if (type.empty()) {
                return std::unexpected("Math variable '" + name + "' is missing 'type'.");
            }
            if (!item.contains("value")) {
                return std::unexpected("Math variable '" + name + "' is missing 'value'.");
            }
            const auto parsed = RuntimeState::DeserializeAnyValue(item["value"]);
            if (!parsed.has_value()) {
                return std::unexpected("Math variable '" + name + "' has an unsupported value.");
            }
            values[name] = *parsed;
        }
        return values;
    }

    [[nodiscard]] std::expected<std::map<std::string, RuntimeState::MathFunctionDefinition>, std::string> ParseMathFunctions(
        const nlohmann::json& node
    ) {
        if (!node.is_array()) {
            return std::unexpected("Field 'math_state.functions' must be a JSON array.");
        }

        std::map<std::string, RuntimeState::MathFunctionDefinition> definitions;
        for (const auto& item : node) {
            if (!item.is_object()) {
                return std::unexpected("Each math function entry must be a JSON object.");
            }
            const std::string name = item.value("name", "");
            const std::string type = item.value("type", "");
            const std::string expression = item.value("expression", "");
            if (name.empty()) {
                return std::unexpected("Math function entry is missing 'name'.");
            }
            if (type != "function") {
                return std::unexpected("Math function '" + name + "' must have type 'function'.");
            }
            if (expression.empty()) {
                return std::unexpected("Math function '" + name + "' is missing 'expression'.");
            }
            if (!item.contains("params") || !item["params"].is_array()) {
                return std::unexpected("Math function '" + name + "' must define a 'params' array.");
            }

            RuntimeState::MathFunctionDefinition definition;
            definition.expression = expression;
            for (const auto& param : item["params"]) {
                if (!param.is_string()) {
                    return std::unexpected("Math function '" + name + "' has a non-string parameter.");
                }
                const std::string paramName = param.get<std::string>();
                if (paramName.empty()) {
                    return std::unexpected("Math function '" + name + "' contains an empty parameter name.");
                }
                definition.params.push_back(paramName);
            }
            definitions[name] = std::move(definition);
        }
        return definitions;
    }

    [[nodiscard]] bool ApplyVariablePolicy(
        std::map<std::string, std::any>& target,
        const std::string& key,
        const std::any& value,
        OverwritePolicy policy) {
        auto it = target.find(key);
        switch (policy) {
        case OverwritePolicy::Overwrite:
            target[key] = value;
            return true;
        case OverwritePolicy::SkipIfExists:
            if (it == target.end()) {
                target.emplace(key, value);
                return true;
            }
            return false;
        case OverwritePolicy::Merge:
            if (it == target.end()) {
                target.emplace(key, value);
                return true;
            }
            if (MergeAnyValue(it->second, value)) {
                return true;
            }
            it->second = value;
            return true;
        }
        return false;
    }

    [[nodiscard]] bool ApplyFunctionPolicy(
        std::map<std::string, RuntimeState::FunctionSignature>& target,
        const std::string& key,
        RuntimeState::FunctionSignature function,
        OverwritePolicy policy) {
        auto it = target.find(key);
        switch (policy) {
        case OverwritePolicy::Overwrite:
            target[key] = std::move(function);
            return true;
        case OverwritePolicy::SkipIfExists:
            if (it == target.end()) {
                target.emplace(key, std::move(function));
                return true;
            }
            return false;
        case OverwritePolicy::Merge:
            if (it == target.end()) {
                target.emplace(key, std::move(function));
                return true;
            }
            return false;
        }
        return false;
    }

    [[nodiscard]] std::optional<nlohmann::json> AnyValueToJson(const Zeri::Core::AnyValue& value) {
        if (!value.has_value()) {
            return nlohmann::json(nullptr);
        }

        const auto& type = value.type();
        if (type == typeid(std::nullptr_t)) {
            return nlohmann::json(nullptr);
        }
        if (type == typeid(std::string)) {
            return nlohmann::json(std::any_cast<std::string>(value));
        }
        if (type == typeid(const char*)) {
            return nlohmann::json(std::string(std::any_cast<const char*>(value)));
        }
        if (type == typeid(char*)) {
            return nlohmann::json(std::string(std::any_cast<char*>(value)));
        }
        if (type == typeid(bool)) {
            return nlohmann::json(std::any_cast<bool>(value));
        }
        if (type == typeid(std::int64_t)) {
            return nlohmann::json(std::any_cast<std::int64_t>(value));
        }
        if (type == typeid(int)) {
            return nlohmann::json(static_cast<std::int64_t>(std::any_cast<int>(value)));
        }
        if (type == typeid(long)) {
            return nlohmann::json(static_cast<std::int64_t>(std::any_cast<long>(value)));
        }
        if (type == typeid(long long)) {
            return nlohmann::json(static_cast<std::int64_t>(std::any_cast<long long>(value)));
        }
        if (type == typeid(double)) {
            return nlohmann::json(std::any_cast<double>(value));
        }
        if (type == typeid(float)) {
            return nlohmann::json(static_cast<double>(std::any_cast<float>(value)));
        }
        if (type == typeid(std::vector<Zeri::Core::AnyValue>)) {
            nlohmann::json out = nlohmann::json::array();
            const auto& values = std::any_cast<const std::vector<Zeri::Core::AnyValue>&>(value);
            for (const auto& item : values) {
                const auto serialized = AnyValueToJson(item);
                if (!serialized.has_value()) {
                    return std::nullopt;
                }
                out.push_back(*serialized);
            }
            return out;
        }
        if (type == typeid(std::map<std::string, Zeri::Core::AnyValue>)) {
            nlohmann::json out = nlohmann::json::object();
            const auto& values = std::any_cast<const std::map<std::string, Zeri::Core::AnyValue>&>(value);
            for (const auto& [key, item] : values) {
                const auto serialized = AnyValueToJson(item);
                if (!serialized.has_value()) {
                    return std::nullopt;
                }
                out[key] = *serialized;
            }
            return out;
        }

        return std::nullopt;
    }

    [[nodiscard]] std::optional<Zeri::Core::AnyValue> JsonToAnyValue(const nlohmann::json& value) {
        if (value.is_null()) {
            return Zeri::Core::AnyValue(std::nullptr_t{});
        }
        if (value.is_boolean()) {
            return Zeri::Core::AnyValue(value.get<bool>());
        }
        if (value.is_number_integer()) {
            return Zeri::Core::AnyValue(value.get<std::int64_t>());
        }
        if (value.is_number_float()) {
            return Zeri::Core::AnyValue(value.get<double>());
        }
        if (value.is_string()) {
            return Zeri::Core::AnyValue(value.get<std::string>());
        }
        if (value.is_array()) {
            std::vector<Zeri::Core::AnyValue> out;
            out.reserve(value.size());
            for (const auto& item : value) {
                const auto converted = JsonToAnyValue(item);
                if (!converted.has_value()) {
                    return std::nullopt;
                }
                out.push_back(*converted);
            }
            return Zeri::Core::AnyValue(std::move(out));
        }
        if (value.is_object()) {
            std::map<std::string, Zeri::Core::AnyValue> out;
            for (const auto& [key, item] : value.items()) {
                const auto converted = JsonToAnyValue(item);
                if (!converted.has_value()) {
                    return std::nullopt;
                }
                out.emplace(key, *converted);
            }
            return Zeri::Core::AnyValue(std::move(out));
        }

        return std::nullopt;
    }

    [[nodiscard]] std::string AnyValueTypeName(const Zeri::Core::AnyValue& value) {
        if (!value.has_value()) {
            return "null";
        }

        const auto& type = value.type();
        if (type == typeid(std::nullptr_t)) return "null";
        if (type == typeid(std::string) || type == typeid(const char*) || type == typeid(char*)) return "string";
        if (type == typeid(bool)) return "bool";
        if (type == typeid(std::int64_t) || type == typeid(int) || type == typeid(long) || type == typeid(long long)) return "int64";
        if (type == typeid(double) || type == typeid(float)) return "double";
        if (type == typeid(std::vector<Zeri::Core::AnyValue>)) return "array";
        if (type == typeid(std::map<std::string, Zeri::Core::AnyValue>)) return "object";
        return "unsupported";
    }

}

namespace Zeri::Core {

    RuntimeState::RuntimeState()
        : m_moduleManager(std::make_unique<Zeri::Modules::ModuleManager>()) {
        auto sessionPath = Zeri::Core::ResolveSessionPath();
        auto loadResult = LoadSession(sessionPath);
        if (!loadResult.has_value()) {
            m_startupWarning = loadResult.error();
        }
    }

    RuntimeState::~RuntimeState() {
        auto saveResult = SaveSession(Zeri::Core::ResolveSessionPath());
        if (!saveResult.has_value()) {
            std::cerr << "[RuntimeState] Failed to save session: " << saveResult.error() << "\n";
        }
    }

    Zeri::Modules::ModuleManager& RuntimeState::GetModuleManager() {
        return *m_moduleManager;
    }

    Zeri::Core::ContextManager& RuntimeState::GetContextManager() {
        return m_contextManager;
    }

    void RuntimeState::SetVariable(VariableScope scope, const std::string& key, const std::any& value) {
        SetVariable(scope, key, value, OverwritePolicy::Overwrite);
    }

    bool RuntimeState::SetVariable(VariableScope scope, const std::string& key, const std::any& value, OverwritePolicy policy) {
        std::unique_lock lock(m_varMutex);
        switch (scope) {
        case VariableScope::Local:
            if (m_localVariables.empty()) {
                m_localVariables.emplace_back();
            }
            return ApplyVariablePolicy(m_localVariables.back(), key, value, policy);
        case VariableScope::Session:
            return ApplyVariablePolicy(m_sessionVariables, key, value, policy);
        case VariableScope::Global:
            return ApplyVariablePolicy(m_globalVariables, key, value, policy);
        case VariableScope::Persisted:
            return ApplyVariablePolicy(m_persistedVariables, key, value, policy);
        }
        return false;
    }

    std::any RuntimeState::GetVariable(VariableScope scope, const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        switch (scope) {
        case VariableScope::Local:
            if (!m_localVariables.empty()) {
                auto it = m_localVariables.back().find(key);
                if (it != m_localVariables.back().end()) {
                    return it->second;
                }
            }
            return {};
        case VariableScope::Session: {
            auto it = m_sessionVariables.find(key);
            return it != m_sessionVariables.end() ? it->second : std::any{};
        }
        case VariableScope::Global: {
            auto it = m_globalVariables.find(key);
            return it != m_globalVariables.end() ? it->second : std::any{};
        }
        case VariableScope::Persisted: {
            auto it = m_persistedVariables.find(key);
            return it != m_persistedVariables.end() ? it->second : std::any{};
        }
        }
        return {};
    }

    bool RuntimeState::HasVariable(VariableScope scope, const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        switch (scope) {
        case VariableScope::Local:
            return !m_localVariables.empty() && m_localVariables.back().contains(key);
        case VariableScope::Session:
            return m_sessionVariables.contains(key);
        case VariableScope::Global:
            return m_globalVariables.contains(key);
        case VariableScope::Persisted:
            return m_persistedVariables.contains(key);
        }
        return false;
    }

    void RuntimeState::SetVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Local, key, value);
    }

    std::any RuntimeState::GetVariable(const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        if (!m_localVariables.empty()) {
            auto it = m_localVariables.back().find(key);
            if (it != m_localVariables.back().end()) {
                return it->second;
            }
        }

        auto sessionIt = m_sessionVariables.find(key);
        if (sessionIt != m_sessionVariables.end()) {
            return sessionIt->second;
        }

        auto globalIt = m_globalVariables.find(key);
        if (globalIt != m_globalVariables.end()) {
            return globalIt->second;
        }

        auto mathIt = m_mathVariables.find(key);
        if (mathIt != m_mathVariables.end()) {
            return mathIt->second;
        }

        auto persistedIt = m_persistedVariables.find(key);
        if (persistedIt != m_persistedVariables.end()) {
            return persistedIt->second;
        }

        return {};
    }

    bool RuntimeState::HasVariable(const std::string& key) const {
        std::shared_lock lock(m_varMutex);
        if (!m_localVariables.empty() && m_localVariables.back().contains(key)) {
            return true;
        }
        if (m_sessionVariables.contains(key)) {
            return true;
        }
        if (m_globalVariables.contains(key)) {
            return true;
        }
        if (m_mathVariables.contains(key)) {
            return true;
        }
        return m_persistedVariables.contains(key);
    }

    bool RuntimeState::PromoteVariable(const std::string& key, VariableScope targetScope, OverwritePolicy policy) {
        if (targetScope == VariableScope::Local) {
            return false;
        }

        std::unique_lock lock(m_varMutex);
        if (m_localVariables.empty()) {
            return false;
        }

        auto& local = m_localVariables.back();
        auto it = local.find(key);
        if (it == local.end()) {
            return false;
        }

        const auto &value = it->second;
        bool updated = false;

        switch (targetScope) {
        case VariableScope::Session:
            updated = ApplyVariablePolicy(m_sessionVariables, key, value, policy);
            break;
        case VariableScope::Global:
            updated = ApplyVariablePolicy(m_globalVariables, key, value, policy);
            break;
        case VariableScope::Persisted:
            updated = ApplyVariablePolicy(m_persistedVariables, key, value, policy);
            break;
        default:
            break;
        }

        if (!updated) {
            return false;
        }

        local.erase(it);
        return true;
    }

    void RuntimeState::SetGlobalVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Global, key, value);
    }

    std::any RuntimeState::GetGlobalVariable(const std::string& key) const {
        return GetVariable(VariableScope::Global, key);
    }

    bool RuntimeState::HasGlobalVariable(const std::string& key) const {
        return HasVariable(VariableScope::Global, key);
    }

    void RuntimeState::SetSessionVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Session, key, value);
    }

    std::any RuntimeState::GetSessionVariable(const std::string& key) const {
        return GetVariable(VariableScope::Session, key);
    }

    bool RuntimeState::HasSessionVariable(const std::string& key) const {
        return HasVariable(VariableScope::Session, key);
    }

    void RuntimeState::SetPersistedVariable(const std::string& key, const std::any& value) {
        SetVariable(VariableScope::Persisted, key, value);
    }

    std::any RuntimeState::GetPersistedVariable(const std::string& key) const {
        return GetVariable(VariableScope::Persisted, key);
    }

    bool RuntimeState::HasPersistedVariable(const std::string& key) const {
        return HasVariable(VariableScope::Persisted, key);
    }

    void RuntimeState::SetMathVariable(const std::string& key, const std::any& value) {
        std::unique_lock lock(m_varMutex);
        m_mathVariables[key] = value;
    }

    std::map<std::string, std::any> RuntimeState::GetMathVariables() const {
        std::shared_lock lock(m_varMutex);
        return m_mathVariables;
    }

    std::optional<AnyValue> RuntimeState::GetShared(const std::string& key) const {
        std::shared_lock lock(m_sharedMutex);
        const auto it = m_sharedVariables.find(key);
        if (it == m_sharedVariables.end()) {
            return std::nullopt;
        }
        return it->second;
    }

    void RuntimeState::SetShared(const std::string& key, const AnyValue& value) {
        std::unique_lock lock(m_sharedMutex);
        m_sharedVariables[key] = value;
    }

    std::vector<std::pair<std::string, AnyValue>> RuntimeState::ListShared() const {
        std::shared_lock lock(m_sharedMutex);
        std::vector<std::pair<std::string, AnyValue>> entries;
        entries.reserve(m_sharedVariables.size());
        for (const auto& [key, value] : m_sharedVariables) {
            entries.emplace_back(key, value);
        }
        return entries;
    }

    void RuntimeState::DeleteShared(const std::string& key) {
        std::unique_lock lock(m_sharedMutex);
        m_sharedVariables.erase(key);
    }

    void RuntimeState::ClearShared() {
        std::unique_lock lock(m_sharedMutex);
        m_sharedVariables.clear();
    }

    std::optional<nlohmann::json> RuntimeState::SerializeAnyValue(const AnyValue& value) {
        return AnyValueToJson(value);
    }

    std::optional<AnyValue> RuntimeState::DeserializeAnyValue(const nlohmann::json& value) {
        return JsonToAnyValue(value);
    }

    std::string RuntimeState::DescribeAnyValueType(const AnyValue& value) {
        return AnyValueTypeName(value);
    }

    bool RuntimeState::SetFunction(VariableScope scope, const std::string& name, FunctionSignature function, OverwritePolicy policy) {
        std::unique_lock lock(m_functionMutex);
        bool updated = false;

        switch (scope) {
        case VariableScope::Local:
            if (m_localFunctions.empty()) {
                m_localFunctions.emplace_back();
            }
            updated = ApplyFunctionPolicy(m_localFunctions.back(), name, std::move(function), policy);
            break;
        case VariableScope::Session:
            updated = ApplyFunctionPolicy(m_sessionFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Global:
            updated = ApplyFunctionPolicy(m_globalFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Persisted:
            updated = ApplyFunctionPolicy(m_persistedFunctions, name, std::move(function), policy);
            break;
        }

        if (updated) {
            ++m_functionRevision;
        }

        return updated;
    }

    std::optional<RuntimeState::FunctionSignature> RuntimeState::GetFunction(VariableScope scope, const std::string& name) const {
        std::shared_lock lock(m_functionMutex);
        switch (scope) {
        case VariableScope::Local:
            if (!m_localFunctions.empty()) {
                auto it = m_localFunctions.back().find(name);
                if (it != m_localFunctions.back().end()) {
                    return it->second;
                }
            }
            return std::nullopt;
        case VariableScope::Session: {
            auto it = m_sessionFunctions.find(name);
            return it != m_sessionFunctions.end() ? std::optional<FunctionSignature>{ it->second } : std::nullopt;
        }
        case VariableScope::Global: {
            auto it = m_globalFunctions.find(name);
            return it != m_globalFunctions.end() ? std::optional<FunctionSignature>{ it->second } : std::nullopt;
        }
        case VariableScope::Persisted: {
            auto it = m_persistedFunctions.find(name);
            return it != m_persistedFunctions.end() ? std::optional<FunctionSignature>{ it->second } : std::nullopt;
        }
        }
        return std::nullopt;
    }

    std::optional<RuntimeState::FunctionSignature> RuntimeState::GetFunction(const std::string& name) const {
        std::shared_lock lock(m_functionMutex);

        if (!m_localFunctions.empty()) {
            auto it = m_localFunctions.back().find(name);
            if (it != m_localFunctions.back().end()) {
                return it->second;
            }
        }

        auto sessionIt = m_sessionFunctions.find(name);
        if (sessionIt != m_sessionFunctions.end()) {
            return sessionIt->second;
        }

        auto globalIt = m_globalFunctions.find(name);
        if (globalIt != m_globalFunctions.end()) {
            return globalIt->second;
        }

        auto persistedIt = m_persistedFunctions.find(name);
        if (persistedIt != m_persistedFunctions.end()) {
            return persistedIt->second;
        }

        return std::nullopt;
    }

    bool RuntimeState::HasFunction(VariableScope scope, const std::string& name) const {
        std::shared_lock lock(m_functionMutex);
        switch (scope) {
        case VariableScope::Local:
            return !m_localFunctions.empty() && m_localFunctions.back().contains(name);
        case VariableScope::Session:
            return m_sessionFunctions.contains(name);
        case VariableScope::Global:
            return m_globalFunctions.contains(name);
        case VariableScope::Persisted:
            return m_persistedFunctions.contains(name);
        }
        return false;
    }

    bool RuntimeState::HasFunction(const std::string& name) const {
        std::shared_lock lock(m_functionMutex);
        if (!m_localFunctions.empty() && m_localFunctions.back().contains(name)) {
            return true;
        }
        if (m_sessionFunctions.contains(name)) {
            return true;
        }
        if (m_globalFunctions.contains(name)) {
            return true;
        }
        return m_persistedFunctions.contains(name);
    }

    bool RuntimeState::PromoteFunction(const std::string& name, VariableScope targetScope, OverwritePolicy policy) {
        if (targetScope == VariableScope::Local) {
            return false;
        }

        std::unique_lock lock(m_functionMutex);
        if (m_localFunctions.empty()) {
            return false;
        }

        auto& local = m_localFunctions.back();
        auto it = local.find(name);
        if (it == local.end()) {
            return false;
        }

        auto &function = it->second;
        bool updated = false;

        switch (targetScope) {
        case VariableScope::Session:
            updated = ApplyFunctionPolicy(m_sessionFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Global:
            updated = ApplyFunctionPolicy(m_globalFunctions, name, std::move(function), policy);
            break;
        case VariableScope::Persisted:
            updated = ApplyFunctionPolicy(m_persistedFunctions, name, std::move(function), policy);
            break;
        default:
            break;
        }

        if (!updated) {
            return false;
        }

        local.erase(it);
        ++m_functionRevision;
        return true;
    }

    void RuntimeState::SetGlobalFunction(const std::string& name, FunctionSignature function) {
        SetFunction(VariableScope::Global, name, std::move(function));
    }

    void RuntimeState::SetSessionFunction(const std::string& name, FunctionSignature function) {
        SetFunction(VariableScope::Session, name, std::move(function));
    }

    void RuntimeState::SetPersistedFunction(const std::string& name, FunctionSignature function) {
        SetFunction(VariableScope::Persisted, name, std::move(function));
    }

    void RuntimeState::SetMathFunctionDefinition(const std::string& name, MathFunctionDefinition definition) {
        std::unique_lock lock(m_functionMutex);
        m_mathFunctionDefinitions[name] = std::move(definition);
    }

    std::map<std::string, RuntimeState::MathFunctionDefinition> RuntimeState::GetMathFunctionDefinitions() const {
        std::shared_lock lock(m_functionMutex);
        return m_mathFunctionDefinitions;
    }

    std::map<std::string, RuntimeState::FunctionSignature> RuntimeState::GetResolvedFunctions() const {
        std::shared_lock lock(m_functionMutex);
        std::map<std::string, FunctionSignature> resolved;

        for (const auto& [name, function] : m_persistedFunctions) {
            resolved[name] = function;
        }
        for (const auto& [name, function] : m_globalFunctions) {
            resolved[name] = function;
        }
        for (const auto& [name, function] : m_sessionFunctions) {
            resolved[name] = function;
        }
        if (!m_localFunctions.empty()) {
            for (const auto& [name, function] : m_localFunctions.back()) {
                resolved[name] = function;
            }
        }

        return resolved;
    }

    std::size_t RuntimeState::GetFunctionRegistryRevision() const {
        return m_functionRevision.load();
    }

    std::map<std::string, std::any> RuntimeState::GetCurrentLocalVariables() const {
        std::shared_lock lock(m_varMutex);
        if (m_localVariables.empty()) return {};
        return m_localVariables.back();
    }

    std::map<std::string, RuntimeState::FunctionSignature> RuntimeState::GetCurrentLocalFunctions() const {
        std::shared_lock lock(m_functionMutex);
        if (m_localFunctions.empty()) return {};
        return m_localFunctions.back();
    }

    void RuntimeState::SetActiveContext(const std::string& contextName) {
        std::unique_lock lock(m_activeContextMutex);
        m_activeContext = contextName;
    }

    std::string RuntimeState::GetActiveContext() const {
        std::shared_lock lock(m_activeContextMutex);
        return m_activeContext;
    }

    void RuntimeState::PushContext(Zeri::Engines::ContextPtr context) {
        std::scoped_lock lock(m_varMutex, m_functionMutex);
        m_contextManager.Push(std::move(context));
        m_localVariables.emplace_back();
        m_localFunctions.emplace_back();
    }

    void RuntimeState::PopContext() {
        std::scoped_lock lock(m_varMutex, m_functionMutex);
        const auto beforeSize = m_contextManager.Size();
        if (beforeSize > 1) {
            m_contextManager.Pop();
            if (m_localVariables.size() > 1) {
                m_localVariables.pop_back();
            }
            if (m_localFunctions.size() > 1) {
                m_localFunctions.pop_back();
            }
        }
    }

    Zeri::Engines::IContext* RuntimeState::GetCurrentContext() const {
        return m_contextManager.Current();
    }

    bool RuntimeState::HasContexts() const {
        return !m_contextManager.IsEmpty();
    }

    void RuntimeState::RequestExit() {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        m_exitRequested = true;
    }

    bool RuntimeState::IsExitRequested() const {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        return m_exitRequested;
    }

    bool RuntimeState::WasSessionCorrupted() const {
        std::lock_guard<std::mutex> lock(m_lifecycleMutex);
        return m_sessionCorrupted;
    }

    std::expected<nlohmann::json, std::string> RuntimeState::ExportSessionState() const {
        std::shared_lock varLock(m_varMutex);
        std::shared_lock functionLock(m_functionMutex);

        nlohmann::json root = nlohmann::json::object();
        root["schema_version"] = kRuntimeStateSchemaVersion;
        root["persisted_variables"] = nlohmann::json::object();
        root["global_variables"] = nlohmann::json::object();
        root["math_state"] = {
            {"variables", nlohmann::json::array()},
            {"functions", nlohmann::json::array()}
        };

        for (const auto& [key, value] : m_persistedVariables) {
            const auto serialized = SerializeAnyValue(value);
            if (!serialized.has_value()) {
                return std::unexpected("Cannot serialize persisted variable '" + key + "' (unsupported type).");
            }
            root["persisted_variables"][key] = *serialized;
        }

        for (const auto& [key, value] : m_globalVariables) {
            const auto serialized = SerializeAnyValue(value);
            if (!serialized.has_value()) {
                return std::unexpected("Cannot serialize global variable '" + key + "' (unsupported type).");
            }
            root["global_variables"][key] = *serialized;
        }

        for (const auto& [key, value] : m_mathVariables) {
            const auto serialized = SerializeAnyValue(value);
            if (!serialized.has_value()) {
                return std::unexpected("Cannot serialize math variable '" + key + "' (unsupported type).");
            }
            root["math_state"]["variables"].push_back({
                {"name", key},
                {"type", DescribeAnyValueType(value)},
                {"value", *serialized}
                });
        }

        for (const auto& [name, definition] : m_mathFunctionDefinitions) {
            root["math_state"]["functions"].push_back({
                {"name", name},
                {"type", "function"},
                {"params", definition.params},
                {"expression", definition.expression}
                });
        }

        return root;
    }

    std::expected<void, std::string> RuntimeState::ImportSessionState(const nlohmann::json& root) {
        if (!root.is_object()) {
            return std::unexpected("Session state must be a JSON object.");
        }

        bool legacySchema = !root.contains("schema_version");
        nlohmann::json persistedNode = nlohmann::json::object();
        nlohmann::json globalNode = nlohmann::json::object();
        nlohmann::json mathVariablesNode = nlohmann::json::array();
        nlohmann::json mathFunctionsNode = nlohmann::json::array();

        if (legacySchema) {
            persistedNode = root;
        } else {
            if (!root["schema_version"].is_number_integer()) {
                return std::unexpected("Field 'schema_version' must be an integer.");
            }
            const int schemaVersion = root["schema_version"].get<int>();
            if (schemaVersion <= 0 || schemaVersion > kRuntimeStateSchemaVersion) {
                return std::unexpected("Unsupported session schema_version: " + std::to_string(schemaVersion) + ".");
            }

            if (root.contains("persisted_variables")) {
                persistedNode = root["persisted_variables"];
            }
            if (root.contains("global_variables")) {
                globalNode = root["global_variables"];
            }
            if (root.contains("math_state")) {
                if (!root["math_state"].is_object()) {
                    return std::unexpected("Field 'math_state' must be a JSON object.");
                }
                const auto& mathState = root["math_state"];
                if (mathState.contains("variables")) {
                    mathVariablesNode = mathState["variables"];
                }
                if (mathState.contains("functions")) {
                    mathFunctionsNode = mathState["functions"];
                }
            }
        }

        auto persistedResult = ParseAnyMap(persistedNode, "persisted_variables");
        if (!persistedResult.has_value()) {
            return std::unexpected(persistedResult.error());
        }
        auto globalResult = ParseAnyMap(globalNode, "global_variables");
        if (!globalResult.has_value()) {
            return std::unexpected(globalResult.error());
        }
        auto mathVariablesResult = ParseMathVariables(mathVariablesNode);
        if (!mathVariablesResult.has_value()) {
            return std::unexpected(mathVariablesResult.error());
        }
        auto mathFunctionsResult = ParseMathFunctions(mathFunctionsNode);
        if (!mathFunctionsResult.has_value()) {
            return std::unexpected(mathFunctionsResult.error());
        }

        std::map<std::string, FunctionSignature> compiledMathFunctions;
        for (const auto& [name, definition] : *mathFunctionsResult) {
            auto compiled = CompileMathFunction(definition);
            if (!compiled.has_value()) {
                return std::unexpected("Math function '" + name + "' failed to compile during load: " + compiled.error());
            }
            compiledMathFunctions[name] = *compiled;
        }

        {
            std::scoped_lock lock(m_varMutex, m_functionMutex, m_sharedMutex);
            while (m_contextManager.Size() > 1) {
                m_contextManager.Pop();
            }
            m_localVariables.clear();
            m_sessionVariables.clear();
            m_globalVariables = std::move(*globalResult);
            m_persistedVariables = std::move(*persistedResult);
            m_mathVariables = std::move(*mathVariablesResult);
            m_localVariables.emplace_back();

            m_localFunctions.clear();
            m_sessionFunctions.clear();
            m_globalFunctions = std::move(compiledMathFunctions);
            m_persistedFunctions.clear();
            m_mathFunctionDefinitions = std::move(*mathFunctionsResult);
            m_localFunctions.emplace_back();
            ++m_functionRevision;

            m_sharedVariables.clear();
        }
        {
            std::unique_lock lock(m_activeContextMutex);
            m_activeContext = "global";
        }

        return {};
    }

    std::expected<void, std::string> RuntimeState::SaveSession(const std::filesystem::path& path) const {
        auto exportResult = ExportSessionState();
        if (!exportResult.has_value()) {
            return std::unexpected("Save failed: " + exportResult.error());
        }

        try {
            const auto parent = path.parent_path();
            if (!parent.empty()) {
                std::filesystem::create_directories(parent);
            }

            const std::filesystem::path tempPath = path.string() + ".tmp";
            const std::filesystem::path backupPath = path.string() + ".bak";
            const std::string serialized = exportResult->dump(2);

            std::string durableError;
            if (!PersistDurableFile(tempPath, serialized, durableError)) {
                throw std::runtime_error(durableError + " " + tempPath.string());
            }

            ApplySavePauseHook();

            if (std::filesystem::exists(path)) {
                if (std::filesystem::exists(backupPath)) {
                    std::filesystem::remove(backupPath);
                }
                std::filesystem::rename(path, backupPath);
            }

            std::filesystem::rename(tempPath, path);
            return {};
        } catch (const std::exception& e) {
            try {
                std::filesystem::remove(path.string() + ".tmp");
            } catch (const std::exception& cleanupEx) {
                std::cerr << "[RuntimeState] Failed to cleanup temp file after save error: " << cleanupEx.what() << "\n";
            }
            return std::unexpected(std::string("Save failed: ") + e.what());
        }
    }

    std::expected<void, std::string> RuntimeState::LoadSession(const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) {
            return std::unexpected("State file not found: " + path.string());
        }

        try {
            std::ifstream primaryFile(path);
            if (!primaryFile.is_open()) {
                return std::unexpected("Failed to open file for reading: " + path.string());
            }

            nlohmann::json root = nlohmann::json::parse(primaryFile);
            auto importResult = ImportSessionState(root);
            if (!importResult.has_value()) {
                {
                    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
                    m_sessionCorrupted = true;
                }
                return std::unexpected("Load failed: " + importResult.error());
            }
            {
                std::lock_guard<std::mutex> lock(m_lifecycleMutex);
                m_sessionCorrupted = false;
            }
            return {};
        } catch (const nlohmann::json::parse_error&) {
            {
                std::lock_guard<std::mutex> lock(m_lifecycleMutex);
                m_sessionCorrupted = true;
            }

            const std::filesystem::path backupPath = path.string() + ".bak";
            try {
                std::ifstream backupFile(backupPath);
                if (!backupFile.is_open()) {
                    return std::unexpected("Session corrupted, no valid backup found.");
                }

                nlohmann::json backupRoot = nlohmann::json::parse(backupFile);
                auto importResult = ImportSessionState(backupRoot);
                if (!importResult.has_value()) {
                    return std::unexpected("Session corrupted and backup restore failed: " + importResult.error());
                }
                return std::unexpected("Session file was corrupted; loaded from backup.");
            } catch (const std::exception&) {
                {
                    std::lock_guard<std::mutex> lock(m_lifecycleMutex);
                    m_sessionCorrupted = true;
                }
                return std::unexpected("Session corrupted, no valid backup found.");
            }
        } catch (const std::exception& e) {
            return std::unexpected(std::string("Load failed: ") + e.what());
        }
    }

    void RuntimeState::ResetSession() {
        {
            std::unique_lock lock(m_varMutex);
            m_localVariables.clear();
            m_sessionVariables.clear();
            m_mathVariables.clear();
        }
        {
            std::unique_lock lock(m_functionMutex);
            m_localFunctions.clear();
            m_sessionFunctions.clear();
            m_mathFunctionDefinitions.clear();
            ++m_functionRevision;
        }
        {
            std::scoped_lock lock(m_varMutex, m_functionMutex);
            while (m_contextManager.Size() > 1) {
                m_contextManager.Pop();
            }
            m_localVariables.clear();
            m_localFunctions.clear();
            m_localVariables.emplace_back();
            m_localFunctions.emplace_back();
        }
        {
            std::unique_lock lock(m_sharedMutex);
            m_sharedVariables.clear();
        }
    }

}

/*
RuntimeState Implementation
Handles variable evaluation, context switching and execution state management.
Additionally supports variable and policy merging on contexts, and handles the persistence 
of the runtime state variables and functions in and across the sessions (SaveSession/LoadSession).
Context stack ownership is centralized in ContextManager to avoid dual-stack drift.
Context stack synchronization is internal to ContextManager; RuntimeState protects
only local variable/function frame alignment around push/pop/reset transitions.

ResetSession:
  Clears all local and session variables and functions. Pops the context
  stack back to the root (global) context, preserving a single empty
  local frame. Does not touch global or persisted state.
*/