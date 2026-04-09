package ui

type CompletionEntry struct {
	Command  string
	Synopsis string
}

var SlashCommands = []CompletionEntry{
	{"/help", "Show available commands"},
	{"/clear", "Clear the chat history"},
	{"/exit", "Exit Zeri"},
	{"/context", "List available contexts"},
	{"/back", "Return to previous context"},
	{"/reset", "Reset the current session"},
	{"/status", "Show engine diagnostics"},
}

var ContextCommands = []CompletionEntry{
    {"$code", "Scripting language dispatch hub"},
	{"$customCommand", "Custom user command scope"},
	{"$math", "Mathematical expression engine"},
	{"$sandbox", "Module development environment"},
	{"$setup", "Configuration wizard"},
	{"$global", "Return to root context"},
}

var SandboxCommands = []CompletionEntry{
 {"/open", "Open a file in the configured IDE"},
	{"/set-ide", "Set the preferred IDE"},
	{"/watch", "Watch a path for changes"},
	{"/list", "List all available modules"},
	{"/build", "Build a module using CMake"},
	{"/run", "Run a compiled module"},
}

var CodeCommands = []CompletionEntry{
	{"/lua", "Enter Lua context"},
	{"/python", "Enter Python context"},
	{"/js", "Enter JavaScript context"},
	{"/ts", "Enter TypeScript context"},
	{"/ruby", "Enter Ruby context"},
}

/*
 * CHANGES & RATIONALE
 *
 * What changed:
 *   - Updated SandboxCommands to reflect current SandboxContext commands:
 *     /open, /set-ide, /watch, /list, /build, /run.
 *   - Added CodeCommands for ScriptHub context routing commands.
 *   - Added new context switches $code and $customCommand.
 *
 * Why:
 *   - Autocomplete must mirror current engine commands after Session 2
 *     rollback and ScriptHub introduction.
 *
 * Impact on other components:
 *   - autocomplete.go selects SandboxCommands in sandbox context and
 *     CodeCommands in code context.
 */
