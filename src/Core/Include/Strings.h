#pragma once
#include <string_view>

namespace Zeri::Ui::Config {

    namespace Strings {
        inline constexpr std::string_view DefaultPrompt = "Type a command...";
        inline constexpr std::string_view Welcome = "Welcome to Zeri REPL. Type /help for assistance.";
        inline constexpr std::string_view Goodbye = "Goodbye.";

        inline constexpr std::string_view WizardCancel = "Procedure cancelled (ESC).";
        inline constexpr std::string_view WizardConfirm = "Procedure confirmed (ENTER).";
        inline constexpr std::string_view WizardNext = "Press ENTER to continue...";
        inline constexpr std::string_view WizardSelect = "Use arrows to select, ENTER to confirm.";

        inline constexpr std::string_view UnclosedQuote = "Unclosed quoted string.";
        inline constexpr std::string_view InvalidSyntax = "Invalid syntax. Input must start with '/', '$' or '!'.";
        inline constexpr std::string_view UnknownCmd = "Unknown command. Use /help to see available commands.";
        inline constexpr std::string_view ContextSwitch = "Context switched to: ";

        inline constexpr std::string_view SetupTitle = "--- Configuration Wizard ---";
        inline constexpr std::string_view SetupEditor = "Select your preferred code editor:";
        inline constexpr std::string_view SetupComplete = "Configuration complete. Settings saved.";
    }

}

/*
Strings.h — Centralised constexpr string constants for the REPL UI.

Sections:
  General: DefaultPrompt, Welcome, Goodbye.
  Wizard: WizardCancel, WizardConfirm, WizardNext, WizardSelect.
  Errors: UnclosedQuote, InvalidSyntax, UnknownCmd, ContextSwitch.
  Setup Wizard: SetupTitle, SetupEditor, SetupComplete.

Dipendenze: nessuna (header-only, constexpr).
*/
