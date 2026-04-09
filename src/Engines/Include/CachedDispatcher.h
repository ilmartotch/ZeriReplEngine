#pragma once
#include "Interface/IParser.h"
#include "Interface/IDispatcher.h"
#include <deque>
#include <expected>
#include <string>
#include <unordered_map>

namespace Zeri::Engines::Defaults {

    struct DispatchResult {
        Command command;
        ExecutionType executionType{ ExecutionType::Unknown };
        bool cacheHit{ false };
    };

    class CachedDispatcher final {
    public:
        CachedDispatcher(IParser& parser, IDispatcher& dispatcher, std::size_t maxEntries = 128);

        [[nodiscard]] std::expected<DispatchResult, ParseError> Dispatch(const std::string& input);
        void Clear();
        void SetMaxEntries(std::size_t maxEntries);

    private:
        struct CacheEntry {
            Command command;
            ExecutionType type{ ExecutionType::Unknown };
        };

        void EnforceLimits();

        IParser& m_parser;
        IDispatcher& m_dispatcher;
        std::unordered_map<std::string, CacheEntry> m_cache;
        std::deque<std::string> m_fifo;
        std::size_t m_maxEntries;
    };
}

/*
CachedDispatcher.h — LRU-cached dispatcher wrapping IParser + IDispatcher.

Responsabilità:
  - Dispatch(): Parses input via IParser, classifies via IDispatcher,
    caches results in an LRU map bounded by maxEntries.
  - Clear(): Empties the cache entirely.
  - SetMaxEntries(): Adjusts capacity, evicting oldest entries if needed.

Dipendenze: IParser, IDispatcher.
*/