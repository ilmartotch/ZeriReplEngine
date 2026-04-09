#include "../Include/TerminalUi.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <format>
#include <memory>
#include <string_view>

#ifdef _WIN32
    #include <Windows.h>
#endif

namespace Zeri::Ui {
    namespace {
        constexpr std::string_view kBlockDelimiterStart = "<<";
        constexpr std::string_view kBlockDelimiterEnd = ">>";
        constexpr std::string_view kContinuationPrompt = "| ";

        struct CommandSpec {
            std::string_view command;
            std::string_view usageHint;
        };

        constexpr std::array<CommandSpec, 8> kGlobalCommands = {
            CommandSpec{ "/help", " — Show help for current context" },
            CommandSpec{ "/context", " — List available contexts" },
            CommandSpec{ "/exit", " — Exit the REPL" },
            CommandSpec{ "/back", " — Return to previous context" },
            CommandSpec{ "/save", " — Save session state to disk" },
            CommandSpec{ "/set", " <key> <value>" },
            CommandSpec{ "/get", " <key>" },
            CommandSpec{ "/lua", " <script>" }
        };

        constexpr std::array<CommandSpec, 7> kMathCommands = {
            CommandSpec{ "/eval", " <expr>" },
            CommandSpec{ "/fn", " f(x) = expression" },
            CommandSpec{ "/vars", " — List variables" },
            CommandSpec{ "/fns", " — List functions" },
            CommandSpec{ "/promote", " <var> <session|global|persisted>" },
            CommandSpec{ "/calc", " <a> <+|-|*|/> <b>" },
            CommandSpec{ "/logic", " <and|or|xor> <true|false> <true|false>" }
        };

        constexpr std::array<CommandSpec, 3> kSandboxCommands = {
            CommandSpec{ "/list", " — List available modules" },
            CommandSpec{ "/build", " <moduleName>" },
            CommandSpec{ "/run", " <moduleName>" }
        };

        constexpr std::array<CommandSpec, 1> kSetupCommands = {
            CommandSpec{ "/start", " — Start configuration wizard" }
        };

        constexpr std::array<std::string_view, 4> kContextCandidates = {
            "$global", "$math", "$sandbox", "$setup"
        };

        [[nodiscard]] std::string_view Trim(std::string_view value) {
            auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
            while (!value.empty() && isSpace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
            return value;
        }

        [[nodiscard]] std::string_view LTrim(std::string_view value) {
            auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
            while (!value.empty() && isSpace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
            return value;
        }

        [[nodiscard]] bool StartsWith(std::string_view value, std::string_view prefix) {
            return value.size() >= prefix.size() && value.substr(0, prefix.size()) == prefix;
        }

        [[nodiscard]] std::string ToLower(std::string_view value) {
            std::string result;
            result.reserve(value.size());
            for (char c : value) {
                result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
            return result;
        }

        [[nodiscard]] std::string_view CurrentStage(std::string_view input) {
            const auto pos = input.find_last_of('|');
            return (pos == std::string_view::npos) ? input : input.substr(pos + 1);
        }

        struct EditState {
            std::vector<std::string_view> tokens;
            std::string_view activeToken;
            bool atNewToken{ false };
        };

        [[nodiscard]] EditState AnalyzeStage(std::string_view stage) {
            EditState st;
            stage = LTrim(stage);

            st.atNewToken = (!stage.empty() && std::isspace(static_cast<unsigned char>(stage.back())) != 0);

            std::size_t i = 0;
            while (i < stage.size()) {
                while (i < stage.size() && std::isspace(static_cast<unsigned char>(stage[i])) != 0) ++i;
                if (i >= stage.size()) break;

                const std::size_t start = i;
                while (i < stage.size() && std::isspace(static_cast<unsigned char>(stage[i])) == 0) ++i;
                st.tokens.emplace_back(stage.substr(start, i - start));
            }

            if (st.atNewToken || st.tokens.empty()) {
                st.activeToken = {};
            } else {
                st.activeToken = st.tokens.back();
            }

            return st;
        }

        template <typename T>
        void AddMatches(std::string_view token, const T& candidates, replxx::Replxx::completions_t& out) {
            for (const auto& c : candidates) {
                const std::string_view candidate = c;
                if (token.empty() || StartsWith(candidate, token)) {
                    out.emplace_back(std::string(candidate));
                }
            }
        }

#ifdef _WIN32
        [[nodiscard]] std::wstring Utf8ToWide(std::string_view input) {
            if (input.empty()) {
                return {};
            }

            const int len = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), nullptr, 0);
            if (len <= 0) {
                return {};
            }

            std::wstring out(static_cast<size_t>(len), L'\0');
            const int written = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, input.data(), static_cast<int>(input.size()), out.data(), len);
            if (written <= 0) {
                return {};
            }

            return out;
        }

        [[nodiscard]] std::string WideToUtf8(std::wstring_view input) {
            if (input.empty()) {
                return {};
            }

            const int len = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
            if (len <= 0) {
                return {};
            }

            std::string out(static_cast<size_t>(len), '\0');
            const int written = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), out.data(), len, nullptr, nullptr);
            if (written <= 0) {
                return {};
            }

            return out;
        }

        bool SetClipboardText(std::string_view text) {
            const std::wstring wide = Utf8ToWide(text);
            if (wide.empty() && !text.empty()) {
                return false;
            }

            if (!OpenClipboard(nullptr)) {
                return false;
            }

            const auto guard = [] { CloseClipboard(); };
            if (!EmptyClipboard()) {
                guard();
                return false;
            }

            const size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
            HGLOBAL block = GlobalAlloc(GMEM_MOVEABLE, bytes);
            if (block == nullptr) {
                guard();
                return false;
            }

            void* mem = GlobalLock(block);
            if (mem == nullptr) {
                GlobalFree(block);
                guard();
                return false;
            }

            std::memcpy(mem, wide.c_str(), bytes);
            GlobalUnlock(block);

            if (SetClipboardData(CF_UNICODETEXT, block) == nullptr) {
                GlobalFree(block);
                guard();
                return false;
            }

            guard();
            return true;
        }

        [[nodiscard]] std::optional<std::string> GetClipboardText() {
            if (!OpenClipboard(nullptr)) {
                return std::nullopt;
            }

            const auto guard = [] { CloseClipboard(); };
            HANDLE data = GetClipboardData(CF_UNICODETEXT);
            if (data == nullptr) {
                guard();
                return std::nullopt;
            }

            const wchar_t* text = static_cast<const wchar_t*>(GlobalLock(data));
            if (text == nullptr) {
                guard();
                return std::nullopt;
            }

            std::wstring wide(text);
            GlobalUnlock(data);
            guard();

            return WideToUtf8(wide);
        }
#else
        [[nodiscard]] bool CommandExists(std::string_view command) {
            const std::string check = "command -v " + std::string(command) + " >/dev/null 2>&1";
            return std::system(check.c_str()) == 0;
        }

        [[nodiscard]] std::optional<std::pair<std::string, std::string>> ResolveClipboardCommands() {
#ifdef __APPLE__
            if (CommandExists("pbcopy") && CommandExists("pbpaste")) {
                return std::pair<std::string, std::string>{ "pbcopy", "pbpaste" };
            }
            return std::nullopt;
#else
            if (CommandExists("wl-copy") && CommandExists("wl-paste")) {
                return std::pair<std::string, std::string>{ "wl-copy", "wl-paste --no-newline" };
            }

            if (CommandExists("xclip")) {
                return std::pair<std::string, std::string>{ "xclip -selection clipboard", "xclip -selection clipboard -o" };
            }

            if (CommandExists("xsel")) {
                return std::pair<std::string, std::string>{ "xsel --clipboard --input", "xsel --clipboard --output" };
            }

            return std::nullopt;
#endif
        }

        bool SetClipboardText(std::string_view text) {
            const auto commands = ResolveClipboardCommands();
            if (!commands.has_value()) {
                return false;
            }

            std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(commands->first.c_str(), "w"), pclose);
            if (!pipe) {
                return false;
            }

            const size_t written = std::fwrite(text.data(), sizeof(char), text.size(), pipe.get());
            return written == text.size();
        }

        [[nodiscard]] std::optional<std::string> GetClipboardText() {
            const auto commands = ResolveClipboardCommands();
            if (!commands.has_value()) {
                return std::nullopt;
            }

            std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(commands->second.c_str(), "r"), pclose);
            if (!pipe) {
                return std::nullopt;
            }

            std::string output;
            std::array<char, 4096> buffer{};
            while (std::fgets(buffer.data(), static_cast<int>(buffer.size()), pipe.get()) != nullptr) {
                output += buffer.data();
            }

            return output;
        }
#endif

        template <typename T>
        void AddCommandMatches(std::string_view token, const T& specs, replxx::Replxx::completions_t& out) {
            for (const auto& spec : specs) {
                if (token.empty() || StartsWith(spec.command, token)) {
                    out.emplace_back(std::string(spec.command));
                }
            }
        }

        template <typename T>
        [[nodiscard]] std::optional<std::string_view> FindHint(std::string_view command, const T& specs) {
            for (const auto& spec : specs) {
                if (spec.command == command) {
                    return spec.usageHint;
                }
            }
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string_view> FindUniqueCommandByPrefix(
            std::string_view prefix,
            const std::vector<std::string_view>& pool
        ) {
            std::string_view found{};
            std::size_t count = 0;
            for (auto cmd : pool) {
                if (StartsWith(cmd, prefix)) {
                    found = cmd;
                    ++count;
                    if (count > 1) return std::nullopt;
                }
            }
            if (count == 1) return found;
            return std::nullopt;
        }

        [[nodiscard]] std::optional<std::string> ReadEnvVar(std::string_view name) {
#ifdef _WIN32
            char* value = nullptr;
            size_t len = 0;
            const auto key = std::string(name);
            if (_dupenv_s(&value, &len, key.c_str()) != 0 || value == nullptr) {
                return std::nullopt;
            }

            std::string out{ value };
            std::free(value);

            if (out.empty()) {
                return std::nullopt;
            }
            return out;
#else
            const auto key = std::string(name);
            const char* value = std::getenv(key.c_str());
            if (value == nullptr || *value == '\0') {
                return std::nullopt;
            }
            return std::string(value);
#endif
        }
    }

    TerminalUi::TerminalUi()
        : m_historyPath(ResolveHistoryPath()) {
        m_replxx.set_max_history_size(1000);
        m_replxx.set_max_hint_rows(4);

        m_replxx.set_completion_callback(
            [this](const std::string& input, int& contextLen) {
                return BuildCompletions(input, contextLen);
            }
        );

        m_replxx.set_hint_callback(
            [this](const std::string& input, int& contextLen, replxx::Replxx::Color& color) {
                return BuildHints(input, contextLen, color);
            }
        );

        m_replxx.set_highlighter_callback(
            [this](const std::string& input, replxx::Replxx::colors_t& colors) {
                ApplyHighlight(input, colors);
            }
        );

        m_replxx.bind_key(
            replxx::Replxx::KEY::control('C'),
            [this](char32_t) {
                const auto state = m_replxx.get_state();
                const std::string currentLine = state.text() == nullptr ? "" : std::string(state.text());
                if (!currentLine.empty()) {
                    (void)SetClipboardText(currentLine);
                }
                return replxx::Replxx::ACTION_RESULT::CONTINUE;
            }
        );

        m_replxx.bind_key(
            replxx::Replxx::KEY::control('X'),
            [this](char32_t) {
                const auto state = m_replxx.get_state();
                const std::string currentLine = state.text() == nullptr ? "" : std::string(state.text());
                if (!currentLine.empty()) {
                    (void)SetClipboardText(currentLine);
                    m_replxx.set_state(replxx::Replxx::State("", 0));
                }
                return replxx::Replxx::ACTION_RESULT::CONTINUE;
            }
        );

        m_replxx.bind_key(
            replxx::Replxx::KEY::control('V'),
            [this](char32_t) {
                const auto pasted = GetClipboardText();
                if (!pasted.has_value() || pasted->empty()) {
                    return replxx::Replxx::ACTION_RESULT::CONTINUE;
                }

                for (unsigned char c : *pasted) {
                    m_replxx.emulate_key_press(static_cast<char32_t>(c));
                }
                return replxx::Replxx::ACTION_RESULT::CONTINUE;
            }
        );

        if (!m_historyPath.empty()) {
            std::error_code ec;
            if (const auto parent = m_historyPath.parent_path(); !parent.empty()) {
                std::filesystem::create_directories(parent, ec);
            }
            (void)m_replxx.history_load(m_historyPath.string());
        }
    }

    TerminalUi::~TerminalUi() {
        PersistHistory();
    }

    void TerminalUi::Write(const std::string& text) {
        std::cout << std::format("{}", text);
    }

    void TerminalUi::WriteLine(const std::string& text) {
        std::cout << std::format("{}\n", text);
    }

    void TerminalUi::WriteError(const std::string& text) {
        std::cout << std::format("\033[31mError: {}\033[0m\n", text);
    }

    void TerminalUi::WriteSuccess(const std::string& text) {
        std::cout << std::format("\033[32m[ZERI] {}\033[0m\n", text);
    }

    void TerminalUi::WriteInfo(const std::string& text) {
        std::cout << std::format("\033[34m[INFO] {}\033[0m\n", text);
    }

    bool TerminalUi::Confirm(const std::string& prompt, bool default_value) {
        std::cout << std::format("{} (y/n) [{}]: ", prompt, default_value ? "y" : "n");
        std::string input;
        if (!std::getline(std::cin, input)) return default_value;
        if (input.empty()) return default_value;
        return (input[0] == 'y' || input[0] == 'Y');
    }

    std::optional<int> TerminalUi::SelectMenu(const std::string& title, const std::vector<std::string>& options) {
        std::cout << std::format("\033[33m{}\033[0m\n", title);
        for (size_t i = 0; i < options.size(); ++i) {
            std::cout << std::format("{}. {}\n", i + 1, options[i]);
        }
        std::cout << std::format("Select option (1-{}): ", options.size());

        std::string input;
        if (!std::getline(std::cin, input)) return std::nullopt;
        try {
            int val = std::stoi(input) - 1;
            if (val >= 0 && val < static_cast<int>(options.size())) return val;
        } catch (...) {}
        return std::nullopt;
    }

    std::optional<std::string> TerminalUi::ReadEditorLine(const std::string& prompt, bool addToHistory) {
        const char* raw = m_replxx.input(prompt.c_str());
        if (raw == nullptr) {
            return std::nullopt;
        }

        std::string line{ raw };
        if (addToHistory && !Trim(line).empty()) {
            m_replxx.history_add(line);
        }
        return line;
    }

    std::optional<std::string> TerminalUi::ReadBlock() {
        std::string block;
        bool first = true;

        while (true) {
            auto lineOpt = ReadEditorLine(std::string(kContinuationPrompt), false);
            if (!lineOpt.has_value()) return std::nullopt;
            if (Trim(*lineOpt) == kBlockDelimiterEnd) break;

            if (!first) block.push_back('\n');
            block += *lineOpt;
            first = false;
        }

        return block;
    }

    std::optional<std::string> TerminalUi::ReadLine(const std::string& prompt) {
        UpdateActiveContextFromPrompt(prompt);

        auto inputOpt = ReadEditorLine(prompt, true);
        if (!inputOpt.has_value()) {
            return std::nullopt;
        }

        std::string input = *inputOpt;
        auto trimmed = Trim(input);

        if (trimmed == kBlockDelimiterStart) {
            return ReadBlock();
        }

        if (trimmed.ends_with(kBlockDelimiterStart)) {
            auto headerView = trimmed.substr(0, trimmed.size() - kBlockDelimiterStart.size());
            std::string header{ Trim(headerView) };

            auto block = ReadBlock();
            if (!block.has_value()) return std::nullopt;
            if (header.empty()) return block.value();

            return header + "\n" + block.value();
        }

        return input;
    }

    replxx::Replxx::completions_t TerminalUi::BuildCompletions(const std::string& input, int& contextLen) const {
        replxx::Replxx::completions_t completions;

        const auto stage = CurrentStage(input);
        const auto st = AnalyzeStage(stage);
        const std::string_view token = st.activeToken;
        contextLen = static_cast<int>(token.size());

        if (st.tokens.empty()) {
            completions.emplace_back("/");
            completions.emplace_back("$");
            completions.emplace_back("!");
            return completions;
        }

        const std::string_view first = st.tokens.front();

        if (!token.empty() && token.front() == '$') {
            AddMatches(token, kContextCandidates, completions);
        }

        const bool editingFirstToken = (st.tokens.size() == 1 && !st.atNewToken);
        if (editingFirstToken && first.front() == '/') {
            AddCommandMatches(token, kGlobalCommands, completions);

            switch (m_activeContext) {
            case ReplContext::Math:
                AddCommandMatches(token, kMathCommands, completions);
                break;
            case ReplContext::Sandbox:
                AddCommandMatches(token, kSandboxCommands, completions);
                break;
            case ReplContext::Setup:
                AddCommandMatches(token, kSetupCommands, completions);
                break;
            case ReplContext::Global:
                break;
            }
        }

        if (!st.tokens.empty() && first == "/logic") {
            const std::size_t argIndex = st.atNewToken ? (st.tokens.size() - 1) : (st.tokens.size() - 2);

            if (argIndex == 0) {
                static constexpr std::array<std::string_view, 3> ops = { "and", "or", "xor" };
                AddMatches(token, ops, completions);
            } else if (argIndex == 1 || argIndex == 2) {
                static constexpr std::array<std::string_view, 2> bools = { "true", "false" };
                AddMatches(token, bools, completions);
            }
        }

        std::sort(
            completions.begin(),
            completions.end(),
            [](const replxx::Replxx::Completion& lhs, const replxx::Replxx::Completion& rhs) {
                if (lhs.text() == rhs.text()) {
                    return static_cast<int>(lhs.color()) < static_cast<int>(rhs.color());
                }
                return lhs.text() < rhs.text();
            }
        );

        completions.erase(
            std::unique(
                completions.begin(),
                completions.end(),
                [](const replxx::Replxx::Completion& lhs, const replxx::Replxx::Completion& rhs) {
                    return lhs.text() == rhs.text() && lhs.color() == rhs.color();
                }
            ),
            completions.end()
        );

        return completions;
    }

    replxx::Replxx::hints_t TerminalUi::BuildHints(
        const std::string& input,
        int& contextLen,
        replxx::Replxx::Color& color
    ) const {
        replxx::Replxx::hints_t hints;
        color = replxx::Replxx::Color::CYAN;

        const auto stage = CurrentStage(input);
        const auto st = AnalyzeStage(stage);
        contextLen = static_cast<int>(st.activeToken.size());

        if (st.tokens.empty()) {
            hints.emplace_back(" Type /help, $math, $sandbox, $setup");
            return hints;
        }

        if (st.tokens.front().front() == '$' && st.tokens.size() == 1 && !st.atNewToken) {
            if (auto unique = FindUniqueCommandByPrefix(st.activeToken, { "$global", "$math", "$sandbox", "$setup" }); unique.has_value()) {
                hints.emplace_back(std::string(unique.value().substr(st.activeToken.size())));
            }
            return hints;
        }

        if (st.tokens.front().front() != '/') {
            return hints;
        }

        const std::string_view cmd = st.tokens.front();

        auto pushUsage = [&](std::optional<std::string_view> usage) {
            if (usage.has_value() && !usage->empty()) {
                hints.emplace_back(std::string(usage.value()));
            }
        };

        pushUsage(FindHint(cmd, kGlobalCommands));
        switch (m_activeContext) {
        case ReplContext::Math:
            pushUsage(FindHint(cmd, kMathCommands));
            break;
        case ReplContext::Sandbox:
            pushUsage(FindHint(cmd, kSandboxCommands));
            break;
        case ReplContext::Setup:
            pushUsage(FindHint(cmd, kSetupCommands));
            break;
        case ReplContext::Global:
            break;
        }

        if (st.tokens.size() == 1 && !st.atNewToken) {
            std::vector<std::string_view> pool;
            for (const auto& c : kGlobalCommands) pool.push_back(c.command);
            if (m_activeContext == ReplContext::Math) for (const auto& c : kMathCommands) pool.push_back(c.command);
            if (m_activeContext == ReplContext::Sandbox) for (const auto& c : kSandboxCommands) pool.push_back(c.command);
            if (m_activeContext == ReplContext::Setup) for (const auto& c : kSetupCommands) pool.push_back(c.command);

            if (auto unique = FindUniqueCommandByPrefix(st.activeToken, pool); unique.has_value() && unique.value().size() > st.activeToken.size()) {
                hints.emplace_back(std::string(unique.value().substr(st.activeToken.size())));
            }
        }

        return hints;
    }

    void TerminalUi::ApplyHighlight(const std::string& input, replxx::Replxx::colors_t& colors) const {
        std::fill(colors.begin(), colors.end(), replxx::Replxx::Color::DEFAULT);
        if (input.empty()) return;

        auto colorRange = [&colors](std::size_t begin, std::size_t end, replxx::Replxx::Color color) {
            if (begin >= colors.size()) return;
            end = std::min(end, colors.size());
            for (std::size_t i = begin; i < end; ++i) colors[i] = color;
        };

        for (std::size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '|') colorRange(i, i + 1, replxx::Replxx::Color::MAGENTA);
        }

        bool inQuotes = false;
        for (std::size_t i = 0; i < input.size(); ++i) {
            if (input[i] == '"' && (i == 0 || input[i - 1] != '\\')) {
                inQuotes = !inQuotes;
                colors[i] = replxx::Replxx::Color::YELLOW;
                continue;
            }
            if (inQuotes) colors[i] = replxx::Replxx::Color::YELLOW;
        }

        const auto firstTokenEnd = input.find_first_of(" \t|");
        const std::size_t end = (firstTokenEnd == std::string::npos) ? input.size() : firstTokenEnd;

        if (input.front() == '/') colorRange(0, end, replxx::Replxx::Color::GREEN);
        else if (input.front() == '$') colorRange(0, end, replxx::Replxx::Color::CYAN);
        else if (input.front() == '!') colorRange(0, end, replxx::Replxx::Color::RED);
    }

    void TerminalUi::PersistHistory() {
        if (!m_historyPath.empty()) {
            (void)m_replxx.history_save(m_historyPath.string());
        }
    }

    void TerminalUi::UpdateActiveContextFromPrompt(std::string_view prompt) {
        const auto normalizedPrompt = ToLower(prompt);

        if (normalizedPrompt.find("math") != std::string::npos) {
            m_activeContext = ReplContext::Math;
            return;
        }
        if (normalizedPrompt.find("sandbox") != std::string::npos) {
            m_activeContext = ReplContext::Sandbox;
            return;
        }
        if (normalizedPrompt.find("setup") != std::string::npos) {
            m_activeContext = ReplContext::Setup;
            return;
        }
        m_activeContext = ReplContext::Global;
    }

    std::filesystem::path TerminalUi::ResolveHistoryPath() {
        if (auto custom = ReadEnvVar("ZERI_HISTORY_FILE"); custom.has_value()) {
            return std::filesystem::path(*custom);
        }

#ifdef _WIN32
        if (auto home = ReadEnvVar("USERPROFILE"); home.has_value()) {
            return std::filesystem::path(*home) / ".zeri_history";
        }
#else
        if (auto home = ReadEnvVar("HOME"); home.has_value()) {
            return std::filesystem::path(*home) / ".zeri_history";
        }
#endif
        return ".zeri_history";
    }

}

/*
TerminalUi.cpp — Interactive terminal implementation via replxx.

Provides:
  - BuildCompletions: context-aware tab completion matching commands and
    context candidates against the current token prefix.
  - BuildHints: usage hints displayed inline as the user types, showing
    expected arguments or descriptions for the current command prefix.
  - ApplyHighlight: syntax coloring for / (green), $ (cyan), ! (red),
    " (yellow), | (magenta), and numeric literals (yellow).
  - UpdateActiveContextFromPrompt: parses the prompt string to determine
    the active ReplContext enum for context-aware suggestions.

QA Changes:
  - kGlobalCommands expanded 5 -> 8: added /save, /lua, and /context entries.
  - kSandboxCommands[/list] now has a description hint.
  - All hint strings translated from Italian to English.
  - Empty-input hint updated to English.
  - Active-context detection now supports branded prompts ("Zeri - <context>").
*/
