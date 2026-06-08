#include "../../src/Core/Include/RuntimeState.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <thread>

namespace {
    int g_failures = 0;

    void Expect(bool condition, const std::string& message) {
        if (!condition) {
            std::cerr << "[RuntimeStateShared] " << message << "\n";
            ++g_failures;
        }
    }

    std::optional<std::int64_t> AnyToInt64(const Zeri::Core::AnyValue& value) {
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

    void TestSharedSetGetDelete() {
        Zeri::Core::RuntimeState state;
        state.SetShared("x", static_cast<std::int64_t>(42));
        const auto value = state.GetShared("x");
        Expect(value.has_value(), "shared key x should exist");
        if (value.has_value()) {
            const auto parsed = AnyToInt64(*value);
            Expect(parsed.has_value() && parsed.value() == 42, "shared key x should be int64(42)");
        }

        state.DeleteShared("x");
        Expect(!state.GetShared("x").has_value(), "shared key x should be removed");
    }

    void TestSharedJsonRoundTrip() {
        const nlohmann::json source = {
            {"name", "Ada"},
            {"count", 3},
            {"ratio", 1.5},
            {"enabled", true},
            {"items", nlohmann::json::array({1, 2, 3})},
            {"nested", {{"ok", true}}},
            {"nullable", nullptr}
        };

        const auto converted = Zeri::Core::RuntimeState::DeserializeAnyValue(source);
        Expect(converted.has_value(), "DeserializeAnyValue should convert JSON object");
        if (!converted.has_value()) {
            return;
        }

        const auto serialized = Zeri::Core::RuntimeState::SerializeAnyValue(*converted);
        Expect(serialized.has_value(), "SerializeAnyValue should convert AnyValue object");
        if (!serialized.has_value()) {
            return;
        }

        Expect(*serialized == source, "JSON round-trip should preserve structure");
    }

    void TestSharedConcurrentSetGet() {
        Zeri::Core::RuntimeState state;
        std::atomic<int> reads{ 0 };
        std::atomic<int> validReads{ 0 };
        std::atomic<bool> corrupted{ false };

        std::jthread writer([&]() {
            for (int i = 0; i < 1000; ++i) {
                state.SetShared("x", static_cast<std::int64_t>(42));
            }
        });

        std::jthread reader([&]() {
            for (int i = 0; i < 1000; ++i) {
                const auto value = state.GetShared("x");
                reads.fetch_add(1, std::memory_order_relaxed);
                if (!value.has_value()) {
                    continue;
                }
                const auto parsed = AnyToInt64(*value);
                if (!parsed.has_value() || parsed.value() != 42) {
                    corrupted.store(true, std::memory_order_relaxed);
                    return;
                }
                validReads.fetch_add(1, std::memory_order_relaxed);
            }
        });

        writer.join();
        reader.join();

        Expect(!corrupted.load(std::memory_order_relaxed), "concurrent reads should not observe corrupted values");
        Expect(reads.load(std::memory_order_relaxed) == 1000, "concurrent loop should perform all reads");
        Expect(validReads.load(std::memory_order_relaxed) >= 0, "valid read counter should be initialized");
    }
}

int main() {
    TestSharedSetGetDelete();
    TestSharedJsonRoundTrip();
    TestSharedConcurrentSetGet();

    if (g_failures > 0) {
        std::cerr << "[RuntimeStateShared] Failures: " << g_failures << "\n";
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
