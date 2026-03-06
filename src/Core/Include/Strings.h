#pragma once
#include <string_view>
#include <ftxui/screen/color.hpp>

namespace Zeri::Ui::Config {

    struct MessageStyle {
        std::string_view prefix;
        ftxui::Color color;
        bool bold = false;
    };

    namespace Styles {
        inline const MessageStyle Info    { "[INFO] ", ftxui::Color::RGB(109, 122, 147), false };
        inline const MessageStyle Success { "[ZERI] ", ftxui::Color::RGB(45, 219, 19),   true  }; // Electric Yellow-Green
        inline const MessageStyle Error   { "[ERROR] ", ftxui::Color::Red, true  };
        inline const MessageStyle Command { "> ", ftxui::Color::GrayDark, false };
        inline const MessageStyle Warning { "[WARN] ", ftxui::Color::Yellow, false };
    }

    namespace Strings {
        // General
        inline constexpr std::string_view DefaultPrompt = "Type a command...";
        inline constexpr std::string_view Welcome       = "Welcome to Zeri REPL. Type /help for assistance.";
        inline constexpr std::string_view Goodbye       = "Goodbye.";

        // Wizard
        inline constexpr std::string_view WizardCancel  = "Procedure cancelled (ESC).";
        inline constexpr std::string_view WizardConfirm = "Procedure confirmed (ENTER).";
        inline constexpr std::string_view WizardNext    = "Press ENTER to continue...";
        inline constexpr std::string_view WizardSelect  = "Use arrows to select, ENTER to confirm.";

        // Errors
        inline constexpr std::string_view UnclosedQuote = "Unclosed quoted string.";
        inline constexpr std::string_view InvalidSyntax = "Invalid syntax. Input must start with '/', '$' or '!'.";
        inline constexpr std::string_view UnknownCmd    = "Unknown command. Use /help to see available commands.";
        inline constexpr std::string_view ContextSwitch = "Context switched to: ";
        
        // Setup Wizard
        inline constexpr std::string_view SetupTitle    = "--- Configuration Wizard ---";
        inline constexpr std::string_view SetupEditor   = "Select your preferred code editor:";
        inline constexpr std::string_view SetupComplete = "Configuration complete. Settings saved.";
    }

}
