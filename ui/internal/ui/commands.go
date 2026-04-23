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
	Name        string `json:"name"`
	Description string `json:"description"`
}

type helpCatalogCommandEntry struct {
	Command  string `json:"command"`
	Synopsis string `json:"synopsis"`
}

type helpCatalogData struct {
	Contexts  []helpCatalogContextEntry            `json:"contexts"`
	Reachable map[string][]string                  `json:"reachable"`
	Commands  map[string][]helpCatalogCommandEntry `json:"commands"`
}

var (
	helpCatalogOnce sync.Once
	helpCatalog     helpCatalogData
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
			{Name: "math", Description: "Mathematical expression engine"},
			{Name: "sandbox", Description: "Module development environment"},
			{Name: "setup", Description: "Configuration wizard"},
		},
		Reachable: map[string][]string{
			"global": {"global", "code", "customcommand", "math", "sandbox", "setup"},
			"code": {"global", "lua", "python", "js", "ts", "ruby"},
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
				{Command: "/status", Synopsis: "Show engine diagnostics"},
				{Command: "/save", Synopsis: "Save session state to disk"},
				{Command: "/load", Synopsis: "Load a saved session from disk"},
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
			"script": {
				{Command: "/new", Synopsis: "Create a script"},
				{Command: "/edit", Synopsis: "Edit an existing script"},
				{Command: "/show", Synopsis: "Show a saved script"},
				{Command: "/run", Synopsis: "Run a script or last buffer"},
				{Command: "/list", Synopsis: "List saved scripts"},
				{Command: "/delete", Synopsis: "Delete a saved script"},
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

/*
 * What:
 *   - Updated SandboxCommands to reflect current SandboxContext commands:
 *     /open, /set-ide, /watch, /list, /build, /run.
 *   - Added CodeCommands for ScriptHub context routing commands.
 *   - Added new context switches $code and $customCommand.
 *   - SlashCommandsForContext now returns only script-group commands when the
 *     active context is a script language (js, ts, lua, python, ruby), preventing
 *     global-only commands from leaking into script context autocomplete.
 *
 * Impact on other components:
 *   - autocomplete.go selects SandboxCommands in sandbox context and
 *     CodeCommands in code context.
 */
