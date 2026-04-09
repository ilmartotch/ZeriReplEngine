#include "../Include/CachedDispatcher.h"

namespace Zeri::Engines::Defaults {

    CachedDispatcher::CachedDispatcher(IParser& parser, IDispatcher& dispatcher, std::size_t maxEntries)
        : m_parser(parser)
        , m_dispatcher(dispatcher)
        , m_maxEntries(maxEntries) {}

    std::expected<DispatchResult, ParseError> CachedDispatcher::Dispatch(const std::string& input) {
        if (auto it = m_cache.find(input); it != m_cache.end()) {
            return DispatchResult{ .command = it->second.command, .executionType = it->second.type, .cacheHit = true };
        }

        auto parsed = m_parser.Parse(input);
        if (!parsed.has_value()) {
            return std::unexpected(parsed.error());
        }

        const Command& cmd = parsed.value();
        ExecutionType type = m_dispatcher.Classify(cmd);

        CacheEntry entry{ cmd, type };
        auto [it, inserted] = m_cache.emplace(input, std::move(entry));
        if (!inserted) {
            it->second = CacheEntry{ cmd, type };
        } else {
            m_fifo.emplace_back(input);
            EnforceLimits();
        }

        return DispatchResult{ .command = cmd, .executionType = type, .cacheHit = false };
    }

    void CachedDispatcher::Clear() {
        m_cache.clear();
        m_fifo.clear();
    }

    void CachedDispatcher::SetMaxEntries(std::size_t maxEntries) {
        m_maxEntries = maxEntries;
        EnforceLimits();
    }

    void CachedDispatcher::EnforceLimits() {
        while (m_cache.size() > m_maxEntries && !m_fifo.empty()) {
            const std::string& key = m_fifo.front();
            m_cache.erase(key);
            m_fifo.pop_front();
        }
    }
}

/*
CachedDispatcher.cpp — Implementation of LRU-cached dispatcher.

Dispatch():
  Checks the in-memory cache first (O(1) lookup). On cache miss, parses
  via IParser, classifies via IDispatcher, stores result. FIFO eviction
  keeps cache bounded by m_maxEntries.

Dipendenze: IParser, IDispatcher.
*/