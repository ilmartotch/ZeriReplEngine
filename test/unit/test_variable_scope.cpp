#include "../../src/Core/Include/VariableScope.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {
    using Zeri::Core::ScopeLevel;
    using Zeri::Core::TypedValue;
    using Zeri::Core::ValueType;
    using Zeri::Core::VariableScope;

    int g_failures = 0;

    void Expect(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "[VariableScope] " << message << "\n";
            ++g_failures;
        }
    }

    std::optional<std::int64_t> AnyToInt64(const std::any& value) {
        if (!value.has_value()) {
            return std::nullopt;
        }
        if (value.type() == typeid(std::int64_t)) {
            return std::any_cast<std::int64_t>(value);
        }
        if (value.type() == typeid(int)) {
            return static_cast<std::int64_t>(std::any_cast<int>(value));
        }
        if (value.type() == typeid(long long)) {
            return static_cast<std::int64_t>(std::any_cast<long long>(value));
        }
        return std::nullopt;
    }

    void TestSetGetAcrossLevels() {
        VariableScope scope;
        scope.Set("session.key", std::string("session-value"), ScopeLevel::Session);
        scope.Set("global.key", std::string("global-value"), ScopeLevel::Global);
        scope.Set("local.key", std::string("local-value"), ScopeLevel::Local);

        const auto sessionValue = scope.Get("session.key");
        const auto globalValue = scope.Get("global.key");
        const auto localValue = scope.Get("local.key");

        Expect(sessionValue.has_value(), "session key should exist");
        Expect(globalValue.has_value(), "global key should exist");
        Expect(localValue.has_value(), "local key should exist");

        if (sessionValue.has_value()) {
            Expect(std::any_cast<std::string>(*sessionValue) == "session-value", "session value mismatch");
        }
        if (globalValue.has_value()) {
            Expect(std::any_cast<std::string>(*globalValue) == "global-value", "global value mismatch");
        }
        if (localValue.has_value()) {
            Expect(std::any_cast<std::string>(*localValue) == "local-value", "local value mismatch");
        }
    }

    void TestExistsMissingKey() {
        VariableScope scope;
        Expect(!scope.Exists("missing"), "Exists should return false for missing key");
    }

    void TestTypeDeduction() {
        VariableScope scope;
        scope.Set("s", std::string("text"), ScopeLevel::Session);
        scope.Set("i", 42, ScopeLevel::Session);
        scope.Set("f", 3.5, ScopeLevel::Session);
        scope.Set("b", true, ScopeLevel::Session);

        Expect(scope.GetType("s") == ValueType::String, "string type deduction failed");
        Expect(scope.GetType("i") == ValueType::Integer, "integer type deduction failed");
        Expect(scope.GetType("f") == ValueType::Float, "float type deduction failed");
        Expect(scope.GetType("b") == ValueType::Boolean, "bool type deduction failed");
    }

    void TestSetTypedFromString() {
        VariableScope scope;

        const bool setInt = scope.SetTypedFromString("int.v", "128", ValueType::Integer, ScopeLevel::Session);
        const bool setFloat = scope.SetTypedFromString("float.v", "9.75", ValueType::Float, ScopeLevel::Session);
        const bool setBool = scope.SetTypedFromString("bool.v", "true", ValueType::Boolean, ScopeLevel::Session);
        const bool setString = scope.SetTypedFromString("string.v", "abc", ValueType::String, ScopeLevel::Session);
        const bool setInvalidBool = scope.SetTypedFromString("bool.bad", "not_bool", ValueType::Boolean, ScopeLevel::Session);

        Expect(setInt, "SetTypedFromString should parse integer");
        Expect(setFloat, "SetTypedFromString should parse float");
        Expect(setBool, "SetTypedFromString should parse bool");
        Expect(setString, "SetTypedFromString should parse string");
        Expect(!setInvalidBool, "SetTypedFromString should reject invalid bool");

        const auto intTyped = scope.GetTyped("int.v");
        const auto floatTyped = scope.GetTyped("float.v");
        const auto boolTyped = scope.GetTyped("bool.v");
        const auto stringTyped = scope.GetTyped("string.v");

        Expect(intTyped.has_value() && intTyped->type == ValueType::Integer, "typed int metadata mismatch");
        Expect(floatTyped.has_value() && floatTyped->type == ValueType::Float, "typed float metadata mismatch");
        Expect(boolTyped.has_value() && boolTyped->type == ValueType::Boolean, "typed bool metadata mismatch");
        Expect(stringTyped.has_value() && stringTyped->type == ValueType::String, "typed string metadata mismatch");
    }

    void TestPromoteToGlobal() {
        VariableScope scope;
        scope.Set("promote.key", std::string("value"), ScopeLevel::Session);
        const bool promoted = scope.PromoteToGlobal("promote.key");
        Expect(promoted, "PromoteToGlobal should return true for existing key");
        Expect(scope.Exists("promote.key"), "promoted key should still exist");
        Expect(scope.GetLevel("promote.key") == ScopeLevel::Global, "promoted key should be in global scope");
        const auto value = scope.Get("promote.key");
        Expect(value.has_value(), "promoted key should be retrievable");
        if (value.has_value()) {
            Expect(std::any_cast<std::string>(*value) == "value", "promoted value mismatch");
        }
    }

    void TestConcurrentAccess() {
        VariableScope scope;
        scope.Set("counter", static_cast<std::int64_t>(0), ScopeLevel::Session);

        std::atomic<bool> stop{ false };
        std::atomic<bool> inconsistent{ false };
        std::atomic<int> reads{ 0 };
        std::atomic<int> writes{ 0 };

        std::vector<std::jthread> readers;
        readers.reserve(4);
        for (int i = 0; i < 4; ++i) {
            readers.emplace_back([&]() {
                while (!stop.load(std::memory_order_relaxed)) {
                    const auto value = scope.Get("counter");
                    if (value.has_value()) {
                        const auto parsed = AnyToInt64(*value);
                        if (!parsed.has_value()) {
                            inconsistent.store(true, std::memory_order_relaxed);
                            break;
                        }
                        if (parsed.value() < 0) {
                            inconsistent.store(true, std::memory_order_relaxed);
                            break;
                        }
                    }
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        std::jthread writer([&]() {
            std::int64_t v = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                scope.Set("counter", v, ScopeLevel::Session);
                ++v;
                writes.fetch_add(1, std::memory_order_relaxed);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        stop.store(true, std::memory_order_relaxed);

        Expect(!inconsistent.load(std::memory_order_relaxed), "concurrent access detected inconsistent values");
        Expect(reads.load(std::memory_order_relaxed) > 0, "reader threads should perform reads");
        Expect(writes.load(std::memory_order_relaxed) > 0, "writer thread should perform writes");
    }
}

int main() {
    TestSetGetAcrossLevels();
    TestExistsMissingKey();
    TestTypeDeduction();
    TestSetTypedFromString();
    TestPromoteToGlobal();
    TestConcurrentAccess();

    if (g_failures > 0) {
        std::cerr << "[VariableScope] Failures: " << g_failures << "\n";
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
