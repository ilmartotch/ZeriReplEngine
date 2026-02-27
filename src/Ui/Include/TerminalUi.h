#pragma once
#include "ITerminal.h"

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <replxx.hxx>

namespace Zeri::Ui {

    class TerminalUi : public ITerminal {
    public:
        TerminalUi();
        ~TerminalUi() override;

        TerminalUi(const TerminalUi&) = delete;
        TerminalUi& operator=(const TerminalUi&) = delete;
        TerminalUi(TerminalUi&&) = delete;
        TerminalUi& operator=(TerminalUi&&) = delete;

        void Write(const std::string& text) override;
        void WriteLine(const std::string& text) override;
        void WriteError(const std::string& text) override;
        [[nodiscard]] std::optional<std::string> ReadLine(const std::string& prompt) override;

    private:
        enum class ReplContext {
            Global,
            Math,
            Sandbox,
            Setup
        };

        [[nodiscard]] std::optional<std::string> ReadEditorLine(const std::string& prompt, bool addToHistory);
        [[nodiscard]] std::optional<std::string> ReadBlock();

        [[nodiscard]] replxx::Replxx::completions_t BuildCompletions(const std::string& input, int& contextLen) const;
        [[nodiscard]] replxx::Replxx::hints_t BuildHints(const std::string& input, int& contextLen, replxx::Replxx::Color& color) const;

        void ApplyHighlight(const std::string& input, replxx::Replxx::colors_t& colors) const;
        void PersistHistory();
        void UpdateActiveContextFromPrompt(std::string_view prompt);
        static std::filesystem::path ResolveHistoryPath();

        replxx::Replxx m_replxx;
        std::filesystem::path m_historyPath;
        ReplContext m_activeContext{ ReplContext::Global };
    };

}

/*
Concrete implementation of `ITerminal` for standard input/output streams (stdin/stdout).
*/