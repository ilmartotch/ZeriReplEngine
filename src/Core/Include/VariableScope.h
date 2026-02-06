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

	class VariableScope {
	public:
		void Set(const std::string& key, const std::any& value, ScopeLevel level = ScopeLevel::Session);
		[[nodiscard]] std::optional<std::any> Get(const std::string& key) const;
		[[nodiscard]] bool Exists(const std::string& key) const;
		[[nodiscard]] ScopeLevel GetLevel(const std::string& key) const;

		bool PromoteToGlobal(const std::string& key);
		void ClearLocal();

	private:
		std::map<std::string, std::any> m_local;
		std::map<std::string, std::any> m_session;
		std::map<std::string, std::any> m_global;

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