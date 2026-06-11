package ui

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"sync"
)

type CompletionEntry struct {
	Command  string
	Synopsis string
}

type helpCatalogContextEntry struct {
	Name string `json:"name"`
	Description string `json:"description"`
}

type helpCatalogCommandEntry struct {
	Command string `json:"command"`
	Synopsis string `json:"synopsis"`
}

type helpCatalogData struct {
	Contexts []helpCatalogContextEntry            `json:"contexts"`
	Reachable map[string][]string                  `json:"reachable"`
	Commands map[string][]helpCatalogCommandEntry `json:"commands"`
}

type CommandScopeValidation struct {
	Allowed bool
	Command string
	CurrentGroup  string
	AllowedGroups []string
}

var (
	helpCatalogOnce sync.Once
	helpCatalog helpCatalogData
)

func defaultHelpCatalog() helpCatalogData {
	return helpCatalogData{
		Contexts: []helpCatalogContextEntry{
			{Name: "global", Description: "Root command context"},
			{Name: "code", Description: "Scripting language dispatch hub"},
			{Name: "customcommand", Description: "Custom user command scope"},
			{Name: "js", Description: "JavaScript scripting editor and executor"},
			{Name: "ts", Description: "TypeScript scripting editor and executor"},
			{Name: "lua", Description: "Lua scripting editor and executor"},
			{Name: "python", Description: "Python scripting editor and executor"},
			{Name: "ruby", Description: "Ruby scripting editor and executor"},
			{Name: "ai", Description: "AI-assisted code generation context"},
			{Name: "math", Description: "Mathematical expression engine"},
			{Name: "sandbox", Description: "Module development environment"},
			{Name: "setup", Description: "Configuration wizard"},
		},
		Reachable: map[string][]string{
			"global":  {"global", "code", "customcommand", "ai", "math", "sandbox", "setup"},
			"code":    {"global", "lua", "python", "js", "ts", "ruby"},
			"default": {"global"},
		},
		Commands: map[string][]helpCatalogCommandEntry{
			"global": {
				{Command: "/help", Synopsis: "Show help for the active context"},
				{Command: "restart", Synopsis: "Restart and reconnect the engine after a disconnection"},
				{Command: "/runtime-status", Synopsis: "Open Runtime Center with runtime diagnostics"},
				{Command: "/copy last", Synopsis: "Copy the latest non-user output message"},
				{Command: "/copy all", Synopsis: "Copy the full visible message history"},
				{Command: "/clear", Synopsis: "Clear chat history"},
				{Command: "/exit", Synopsis: "Exit the REPL"},
				{Command: "/context", Synopsis: "List reachable contexts"},
				{Command: "/back", Synopsis: "Return to previous context"},
				{Command: "/reset", Synopsis: "Reset the current session"},
				{Command: "/status", Synopsis: "Show engine diagnostics and persistence paths"},
				{Command: "/plugins", Synopsis: "List loaded native and Lua plugins with status"},
				{Command: "/bug report", Synopsis: "Show bug-report instructions"},
				{Command: "/bug snapshot", Synopsis: "Create a diagnostic snapshot file for issue attachments"},
				{Command: "/save", Synopsis: "Save session state to disk"},
				{Command: "/load", Synopsis: "Load a saved session from disk"},
				{Command: "/set", Synopsis: "Store a typed variable in the current scope (--number|--string|--bool)"},
				{Command: "/get", Synopsis: "Read a variable from the current scope"},
			},
			"sandbox": {
				{Command: "/open", Synopsis: "Open a file in the configured IDE"},
				{Command: "/set-ide", Synopsis: "Set the preferred IDE"},
				{Command: "/watch", Synopsis: "Show current sandbox process status"},
				{Command: "/list", Synopsis: "List all available modules"},
				{Command: "/build", Synopsis: "Build a module using CMake"},
				{Command: "/run", Synopsis: "Run a compiled module"},
			},
			"math": {
				{Command: "/eval", Synopsis: "Evaluate an expression"},
				{Command: "/fn", Synopsis: "Define a function"},
				{Command: "/vars", Synopsis: "List variables"},
				{Command: "/fns", Synopsis: "List functions"},
				{Command: "/promote", Synopsis: "Promote a variable scope"},
				{Command: "/calc", Synopsis: "Run a quick arithmetic command"},
				{Command: "/logic", Synopsis: "Run a boolean logic command"},
			},
			"setup": {
				{Command: "/start", Synopsis: "Run the configuration wizard"},
			},
			"customcommand": {
				{Command: "/define", Synopsis: "Define a custom command body"},
				{Command: "/list", Synopsis: "List registered custom commands"},
				{Command: "/run", Synopsis: "Run a custom command by name"},
				{Command: "/show", Synopsis: "Show the custom command body"},
				{Command: "/delete", Synopsis: "Delete a custom command"},
			},
			"code": {
				{Command: "/search", Synopsis: "Search scripts across all supported languages"},
			},
			"script": {
				{Command: "/new", Synopsis: "Create a script"},
				{Command: "/edit", Synopsis: "Edit an existing script"},
				{Command: "/show", Synopsis: "Show a saved script"},
				{Command: "/run", Synopsis: "Run a script or last buffer"},
				{Command: "/list", Synopsis: "List saved scripts"},
				{Command: "/delete", Synopsis: "Delete a saved script"},
				{Command: "/history", Synopsis: "Show script version history"},
				{Command: "/diff", Synopsis: "Show unified diff between two versions"},
				{Command: "/rollback", Synopsis: "Restore an old version as new latest"},
				{Command: "/search", Synopsis: "Search scripts by name, language, or content"},
			},
		},
	}
}

func loadHelpCatalog() helpCatalogData {
	catalog := defaultHelpCatalog()
	path, ok := resolveHelpCatalogPath()
	if !ok {
		return catalog
	}

	raw, err := os.ReadFile(path)
	if err != nil {
		return catalog
	}

	if err = json.Unmarshal(raw, &catalog); err != nil {
		return defaultHelpCatalog()
	}

	return catalog
}

func resolveHelpCatalogPath() (string, bool) {
	explicitPath := strings.TrimSpace(os.Getenv("ZERI_HELP_CATALOG_PATH"))
	if explicitPath != "" {
		candidate := explicitPath
		if !filepath.IsAbs(candidate) {
			if cwd, err := os.Getwd(); err == nil {
				candidate = filepath.Join(cwd, candidate)
			}
		}
		if _, err := os.Stat(candidate); err == nil {
			return candidate, true
		}
	}

	if executablePath, err := os.Executable(); err == nil {
		candidate := filepath.Join(filepath.Dir(executablePath), "help", "help_catalog.json")
		if _, err := os.Stat(candidate); err == nil {
			return candidate, true
		}
	}

	start, err := os.Getwd()
	if err != nil {
		return "", false
	}

	current := start
	for {
		candidate := filepath.Join(current, "help", "help_catalog.json")
		if _, err := os.Stat(candidate); err == nil {
			return candidate, true
		}

		parent := filepath.Dir(current)
		if parent == current {
			break
		}
		current = parent
	}

	return "", false
}

func getHelpCatalog() helpCatalogData {
	helpCatalogOnce.Do(func() {
		helpCatalog = loadHelpCatalog()
	})
	return helpCatalog
}

func normaliseContextName(activeContext string) string {
	name := strings.TrimSpace(strings.TrimPrefix(activeContext, "$"))
	if name == "" {
		return "global"
	}
	lower := strings.ToLower(name)
	lower = strings.TrimPrefix(lower, "zeri::")
	segments := strings.Split(lower, "::")
	if len(segments) > 0 {
		return segments[len(segments)-1]
	}
	return lower
}

func commandGroupForContext(ctx string) string {
	switch ctx {
	case "js", "ts", "lua", "python", "ruby":
		return "script"
	default:
		return ctx
	}
}

func commandsForGroup(group string) []CompletionEntry {
	catalog := getHelpCatalog()
	entries, ok := catalog.Commands[group]
	if !ok {
		return nil
	}

	result := make([]CompletionEntry, 0, len(entries))
	for _, entry := range entries {
		result = append(result, CompletionEntry{Command: entry.Command, Synopsis: entry.Synopsis})
	}
	return result
}

func contextDescriptions() map[string]string {
	catalog := getHelpCatalog()
	result := make(map[string]string, len(catalog.Contexts))
	for _, entry := range catalog.Contexts {
		result[strings.ToLower(entry.Name)] = entry.Description
	}
	return result
}

func SlashCommandsForContext(activeContext string) []CompletionEntry {
	ctx := normaliseContextName(activeContext)
	group := commandGroupForContext(ctx)

	if group == "script" {
		return commandsForGroup("script")
	}

	pool := make([]CompletionEntry, 0, 16)
	pool = append(pool, commandsForGroup("global")...)

	if group != "global" {
		pool = append(pool, commandsForGroup(group)...)
	}

	return pool
}

func ContextCommandsForContext(activeContext string) []CompletionEntry {
	ctx := normaliseContextName(activeContext)
	catalog := getHelpCatalog()
	descriptions := contextDescriptions()

	reachable, ok := catalog.Reachable[ctx]
	if !ok {
		reachable = catalog.Reachable["default"]
	}

	result := make([]CompletionEntry, 0, len(reachable))
	for _, name := range reachable {
		normalized := strings.ToLower(name)
		synopsis, ok := descriptions[normalized]
		if !ok {
			continue
		}
		result = append(result, CompletionEntry{Command: "$" + normalized, Synopsis: synopsis})
	}
	return result
}

var globalSlashCommands = map[string]struct{}{
	"/help": {},
	"/context": {},
	"/back": {},
	"/status": {},
	"/plugins": {},
	"/reset": {},
	"/bug": {},
	"/clear": {},
	"/exit": {},
	"/save": {},
	"/load": {},
	"/copy": {},
	"/runtime-status": {},
	"/set": {},
	"/get": {},
}

var scopedSlashCommands = map[string][]string{
	"/new": {"script"},
	"/edit": {"script"},
	"/show": {"script", "customcommand"},
	"/run": {"script", "sandbox", "customcommand"},
	"/list": {"script", "sandbox", "customcommand"},
	"/delete": {"script", "customcommand"},
	"/define": {"customcommand"},
	"/open": {"sandbox"},
	"/set-ide": {"sandbox"},
	"/watch": {"sandbox"},
	"/build": {"sandbox"},
	"/eval": {"math"},
	"/fn": {"math"},
	"/vars": {"math"},
	"/fns": {"math"},
	"/promote": {"math"},
	"/calc": {"math"},
	"/logic": {"math"},
	"/start": {"setup"},
}

func contextGroupFromActiveContext(activeContext string) string {
	ctx := normaliseContextName(activeContext)
	switch ctx {
	case "js", "ts", "lua", "python", "ruby":
		return "script"
	default:
		return ctx
	}
}

func baseSlashCommand(input string) string {
	trimmed := strings.ToLower(strings.TrimSpace(input))
	if !strings.HasPrefix(trimmed, "/") {
		return ""
	}
	fields := strings.Fields(trimmed)
	if len(fields) == 0 {
		return ""
	}
	return fields[0]
}

func ValidateSlashCommandForContext(activeContext string, input string) CommandScopeValidation {
	base := baseSlashCommand(input)
	if base == "" {
		return CommandScopeValidation{Allowed: true}
	}
	if _, ok := globalSlashCommands[base]; ok {
		return CommandScopeValidation{
			Allowed: true,
			Command: base,
			CurrentGroup: contextGroupFromActiveContext(activeContext),
		}
	}

	allowedGroups, constrained := scopedSlashCommands[base]
	if !constrained {
		return CommandScopeValidation{
			Allowed: true,
			Command: base,
			CurrentGroup: contextGroupFromActiveContext(activeContext),
		}
	}

	currentGroup := contextGroupFromActiveContext(activeContext)
	for _, group := range allowedGroups {
		if group == currentGroup {
			return CommandScopeValidation{
				Allowed: true,
				Command: base,
				CurrentGroup:  currentGroup,
				AllowedGroups: allowedGroups,
			}
		}
	}

	return CommandScopeValidation{
		Allowed: false,
		Command: base,
		CurrentGroup:  currentGroup,
		AllowedGroups: allowedGroups,
	}
}

func CommandScopeDescription(groups []string) string {
	if len(groups) == 0 {
		return "$global"
	}
	labels := make([]string, 0, len(groups))
	for _, group := range groups {
		switch group {
		case "script":
			labels = append(labels, "$js | $ts | $lua | $python | $ruby")
		default:
			labels = append(labels, "$"+group)
		}
	}
	return strings.Join(labels, " ; ")
}

/*
 * What:
 *   - Updated SandboxCommands to reflect current SandboxContext commands:
 *     /open, /set-ide, /watch, /list, /build, /run.
 *   - Added CodeCommands for ScriptHub context routing commands.
 *   - Added new context switches $code and $customCommand.
 *   - SlashCommandsForContext now returns only script-group commands when the
 *     active context is a script language (js, ts, lua, python, ruby), preventing
 *     global-only commands from leaking into script context autocomplete.
 *   - Added customcommand command catalog entries for autocomplete/help
 *     consistency with engine behavior.
 *   - Added command scope validation helpers:
 *     ValidateSlashCommandForContext + CommandScopeDescription.
 *     These provide a strict command hierarchy by context so callers can block
 *     forced out-of-scope commands with actionable errors.
 *
 * Impact on other components:
 *   - autocomplete.go selects SandboxCommands in sandbox context and
 *     CodeCommands in code context.
 *   - cmd/zeri-tui/model.go uses ValidateSlashCommandForContext to enforce
 *     command activation rules at runtime, not only at suggestion level.
 */
