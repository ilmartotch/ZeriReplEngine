#include "../Include/VariableScope.h"
#include "../Include/StringUtils.h"
#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <string_view>
#include <cstdint>
#include <mutex>
#include <shared_mutex>

namespace {
	Zeri::Core::ValueType DeduceType(const std::any& value) {
		const auto& t = value.type();
		if (t == typeid(std::string)) return Zeri::Core::ValueType::String;
		if (t == typeid(const char*) || t == typeid(char*)) return Zeri::Core::ValueType::String;
		if (t == typeid(bool)) return Zeri::Core::ValueType::Boolean;

		if (t == typeid(int) || t == typeid(long) || t == typeid(long long) ||
			t == typeid(unsigned) || t == typeid(unsigned long) || t == typeid(unsigned long long) ||
			t == typeid(std::int64_t) || t == typeid(std::uint64_t)) {
			return Zeri::Core::ValueType::Integer;
		}

		if (t == typeid(float) || t == typeid(double) || t == typeid(long double)) {
			return Zeri::Core::ValueType::Float;
		}

		return Zeri::Core::ValueType::Unknown;
	}

	std::optional<std::any> ParseValue(std::string_view raw, Zeri::Core::ValueType type) {
		switch (type) {
		case Zeri::Core::ValueType::String:
			return std::string(raw);

		case Zeri::Core::ValueType::Boolean: {
			auto lowered = Zeri::Core::Utils::ToLower(raw);
			if (lowered == "true" || lowered == "1") return true;
			if (lowered == "false" || lowered == "0") return false;
			return std::nullopt;
		}

		case Zeri::Core::ValueType::Integer: {
			std::int64_t value{};
			auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
			if (ec == std::errc{} && ptr == raw.data() + raw.size()) {
				return value;
			}
			return std::nullopt;
		}

		case Zeri::Core::ValueType::Float: {
			std::string rawCopy(raw);
			char* end = nullptr;
			double value = std::strtod(rawCopy.c_str(), &end);
			if (end != nullptr && end == rawCopy.c_str() + rawCopy.size()) {
				return value;
			}
			return std::nullopt;
		}

		case Zeri::Core::ValueType::Unknown:
		default:
			return std::nullopt;
		}
	}

}

namespace Zeri::Core {

	std::string VariableScope::NormalizeKey(const std::string& key) const {
		std::string normalized = key;
		std::transform(normalized.begin(), normalized.end(), normalized.begin(),
			[](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
		return normalized;
	}

	void VariableScope::Set(const std::string& key, const std::any& value, ScopeLevel level) {
		SetTyped(key, value, ValueType::Unknown, level);
	}

	void VariableScope::SetTyped(const std::string& key, const std::any& value, ValueType type, ScopeLevel level) {
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		std::string nKey = NormalizeKey(key);
		TypedValue entry{ value, type == ValueType::Unknown ? DeduceType(value) : type };

		switch (level) {
		case ScopeLevel::Local:   m_local[nKey] = entry; break;
		case ScopeLevel::Session: m_session[nKey] = entry; break;
		case ScopeLevel::Global:  m_global[nKey] = entry; break;
		}
	}

	bool VariableScope::SetTypedFromString(const std::string& key, const std::string& rawValue, ValueType type, ScopeLevel level) {
		auto parsed = ParseValue(rawValue, type);
		if (!parsed.has_value()) return false;

		SetTyped(key, parsed.value(), type, level);
		return true;
	}

	std::optional<std::any> VariableScope::Get(const std::string& key) const {
		auto typed = GetTyped(key);
		return typed ? std::optional<std::any>(typed->value) : std::nullopt;
	}

	std::optional<TypedValue> VariableScope::GetTyped(const std::string& key) const {
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		std::string nKey = NormalizeKey(key);

		if (auto it = m_local.find(nKey); it != m_local.end()) return it->second;
		if (auto it = m_session.find(nKey); it != m_session.end()) return it->second;
		if (auto it = m_global.find(nKey); it != m_global.end()) return it->second;

		return std::nullopt;
	}

	bool VariableScope::Exists(const std::string& key) const {
		return GetTyped(key).has_value();
	}

	ScopeLevel VariableScope::GetLevel(const std::string& key) const {
		std::shared_lock<std::shared_mutex> lock(m_mutex);
		std::string nKey = NormalizeKey(key);
		if (m_local.contains(nKey)) return ScopeLevel::Local;
		if (m_session.contains(nKey)) return ScopeLevel::Session;
		return ScopeLevel::Global;
	}

	ValueType VariableScope::GetType(const std::string& key) const {
		auto typed = GetTyped(key);
		return typed ? typed->type : ValueType::Unknown;
	}

	bool VariableScope::PromoteToGlobal(const std::string& key) {
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		std::string nKey = NormalizeKey(key);
		if (auto it = m_local.find(nKey); it != m_local.end()) {
			m_global[nKey] = it->second;
			m_local.erase(it);
			return true;
		}
		if (auto it = m_session.find(nKey); it != m_session.end()) {
			m_global[nKey] = it->second;
			m_session.erase(it);
			return true;
		}
		if (auto it = m_global.find(nKey); it != m_global.end()) {
			m_global[nKey] = it->second;
			return true;
		}
		return false;
	}

	void VariableScope::ClearLocal() {
		std::unique_lock<std::shared_mutex> lock(m_mutex);
		m_local.clear();
	}

}

/*
Implementation of `VariableScope`.
Enforces the scope hierarchy: Local shadows Session shadows Global.
Supports explicit promotion to global scope with warning capability.
(spec: "Promotion from local to global scope is allowed but must be explicit")
*/