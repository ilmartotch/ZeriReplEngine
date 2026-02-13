#pragma once
#include <string>
#include <map>
#include <any>
#include <optional>

namespace Zeri::Core {

	enum class ScopeLevel {
		Local,
		Session,
		Global
	};

	enum class ValueType {
		String,
		Integer,
		Float,
		Boolean,
		Unknown
	};

	struct TypedValue {
		std::any value;
		ValueType type{ ValueType::Unknown };
	};

	class VariableScope {
	public:
		void Set(const std::string& key, const std::any& value, ScopeLevel level = ScopeLevel::Session);
		void SetTyped(const std::string& key, const std::any& value, ValueType type, ScopeLevel level = ScopeLevel::Session);
		bool SetTypedFromString(const std::string& key, const std::string& rawValue, ValueType type, ScopeLevel level = ScopeLevel::Session);

		[[nodiscard]] std::optional<std::any> Get(const std::string& key) const;
		[[nodiscard]] std::optional<TypedValue> GetTyped(const std::string& key) const;
		[[nodiscard]] bool Exists(const std::string& key) const;
		[[nodiscard]] ScopeLevel GetLevel(const std::string& key) const;
		[[nodiscard]] ValueType GetType(const std::string& key) const;

		bool PromoteToGlobal(const std::string& key);
		void ClearLocal();

	private:
		std::map<std::string, TypedValue> m_local;
		std::map<std::string, TypedValue> m_session;
		std::map<std::string, TypedValue> m_global;

		[[nodiscard]] std::string NormalizeKey(const std::string& key) const;
	};
}

/*
Implements the three-tier variable scope model from the meta-language specification.
(spec: "Variables and Scope")

- Local: cleared when context exits
- Session: persists for REPL session lifetime
- Global: persists and can be explicitly promoted

`NormalizeKey` enables case-insensitive resolution as per spec:
"Identifiers preserve case but are resolved case-insensitively by default."
*/