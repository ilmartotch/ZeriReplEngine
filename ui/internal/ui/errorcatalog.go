package ui

import (
	"fmt"
	"sort"
	"strings"
)


type ErrorCatalogEntry struct {
	Code string
	Message string
	Trigger string
	Hint string
}


var errorCatalog = []ErrorCatalogEntry{
	{"CONTEXT-001", "No active context is available.", "Context stack is unexpectedly empty when switching to $global.", "Run /reset to restore the global context."},
	{"CONTEXT-002", "Context switch is not allowed from $from to $to.", "User requests a context that is not reachable from the current context.", "Run /context to list reachable targets."},
	{"CONTEXT-003", "Unknown context: <name>.", "Requested context name is not implemented.", "Run /context and use one listed target."},
	{"CONTEXT-004", "No active context is available.", "Command/expression dispatch occurs while no active context exists.", "Run /reset to restore the global context."},
	{"CONTEXT-005", "Help catalog is unavailable, /help output may be incomplete.", "help/help_catalog.json missing or unreadable at runtime.", "Package help/help_catalog.json next to the executable."},
	{"CONTEXT-006", "Context warning forwarded as error output.", "Warning emitted on the error channel without an explicit ZERI code.", "Run /help in the current context and retry the command."},
	{"CONTEXT-007", "Typed execution error forwarded as standardized error output.", "An ExecutionError reaches the output handler without a ZERI-prefixed code.", "Review the error code and retry with documented syntax."},
	{"CONTEXT-008", "Script editor executor is unavailable.", "/run is invoked inside the editor without an executor instance.", "Re-enter the language context and retry /run."},
	{"CONTEXT-009", "/save requires a script name associated with the editor context.", "/save is used in the editor without a bound script name.", "Open the editor with /new <name> or /edit <name>."},
	{"SESSION-001", "Failed to save session: <error>.", "/save fails while writing session state.", "Verify write permission for the sessions path shown by /status."},
	{"SESSION-002", "Failed to generate bug snapshot: <error>.", "/bug snapshot file generation fails.", "Verify project directory file permissions and retry /bug snapshot."},
	{"SESSION-003", "Previous session file was corrupted or could not be loaded.", "Engine startup loads corrupted session state.", "Run /save to write a fresh session snapshot."},
	{"SESSION-004", "Failed to save session on shutdown: <error>.", "Automatic save during engine shutdown fails.", "Ensure the sessions path is writable (/status)."},
	{"SESSION-010", "Storage initialization failed: <error>.", "Go TUI fails to create/resolve local storage directories at startup.", "Verify write permissions for user data directories."},
	{"SESSION-011", "Unable to resolve executable path: <error>.", "Go TUI cannot resolve its own executable path.", "Reinstall zeri or launch it from a valid installation directory."},
	{"CLI-001", "Unknown option: <option>.", "Unsupported CLI option passed to zeri.", "Use supported options: --no-onboarding, --profile-startup, --exit-after-ready."},
	{"CLI-002", "Missing required --yuumi-pipe <name> argument.", "zeri-engine is started directly without the IPC pipe argument.", "Launch through zeri (TUI) or pass --yuumi-pipe <name> explicitly."},
	{"PARSE-001", "Empty system command.", "! is used without an actual shell command.", "Use !<command> (example: !echo hello)."},
	{"PARSE-002", "<parse message>.", "Meta parser rejects malformed input (for example an unclosed quote).", "Fix the syntax and retry."},
	{"PARSE-003", "Unknown command.", "Pipe operator | or an unsupported token pattern is detected.", "Run /help to see available commands."},
	{"PARSE-004", "Invalid context switch syntax.", "Context switch includes unsupported flags/extra args.", "Use $<context> without flags or extra arguments."},
	{"PARSE-005", "Unrecognized input type.", "Dispatcher receives an unsupported input type enum.", "Run /help to review supported input forms."},
	{"RUNTIME-001", "Failed to execute system command: <command>.", "System shell process cannot be started.", "Confirm the command exists in PATH and retry."},
	{"RUNTIME-002", "System command exited with code: <code>.", "System command returns a non-zero exit status.", "Inspect command output and fix the failing command."},
	{"RUNTIME-003", "Startup diagnostic: <line>.", "Startup diagnostics contain runtime/startup issues.", "Fix the reported dependency or runtime configuration."},
	{"RUNTIME-004", "System prerequisite check failed.", "System guard reports missing required tools.", "Install the missing prerequisites listed below the error."},
	{"RUNTIME-005", "Sandbox process stderr: <chunk>.", "Sandbox external process writes to stderr.", "Review runtime output and fix target/process configuration."},
	{"RUNTIME-006", "Unhandled exception: <error>.", "zeri-engine exits through the top-level std::exception handler.", "Inspect engine logs and rerun the same command to reproduce."},
	{"RUNTIME-007", "Unhandled non-standard exception.", "zeri-engine exits through the top-level catch-all handler.", "Inspect engine logs and report the crash signature."},
	{"RUNTIME-020", "JS/TS runtime stderr: <stderr>.", "JS/TS sidecar execution returns stderr content.", "Inspect script output and runtime dependencies."},
	{"RUNTIME-021", "JS/TS sidecar launch failed, falling back to one-shot execution.", "JS/TS sidecar process cannot start.", "Verify the Bun runtime and bootstrap scripts."},
	{"RUNTIME-022", "JS/TS one-shot stderr: <line>.", "JS/TS one-shot process writes stderr lines.", "Inspect script output and runtime dependencies."},
	{"RUNTIME-023", "Lua runtime stderr: <stderr>.", "Lua sidecar execution returns stderr content.", "Inspect Lua output and runtime dependencies."},
	{"RUNTIME-024", "Lua one-shot stderr: <line>.", "Lua one-shot process writes stderr lines.", "Inspect Lua output and runtime dependencies."},
	{"RUNTIME-025", "Lua sidecar bootstrap not found, falling back to one-shot execution.", "runtime/bootstrap_lua.lua is missing.", "Ensure runtime/bootstrap_lua.lua is packaged."},
	{"RUNTIME-026", "Lua sidecar launch failed, falling back to one-shot execution.", "Lua sidecar process cannot start.", "Verify the luajit installation and executable path."},
	{"RUNTIME-027", "Python one-shot stderr: <line>.", "Python one-shot process writes stderr lines.", "Inspect Python output and runtime dependencies."},
	{"RUNTIME-028", "Python runtime stderr: <stderr>.", "Python sidecar execution returns stderr content.", "Inspect Python output and runtime dependencies."},
	{"RUNTIME-029", "Python sidecar launch failed, falling back to one-shot execution.", "Python sidecar process cannot start.", "Verify the Python installation and bootstrap script availability."},
	{"RUNTIME-030", "Ruby runtime stderr: <stderr>.", "Ruby sidecar execution returns stderr content.", "Inspect Ruby output and runtime dependencies."},
	{"RUNTIME-031", "Ruby one-shot stderr: <line>.", "Ruby one-shot process writes stderr lines.", "Inspect Ruby output and runtime dependencies."},
	{"RUNTIME-032", "Ruby sidecar bootstrap not found, falling back to one-shot execution.", "runtime/bootstrap_ruby.rb is missing.", "Ensure runtime/bootstrap_ruby.rb is packaged."},
	{"RUNTIME-033", "Ruby sidecar launch failed, falling back to one-shot execution.", "Ruby sidecar process cannot start.", "Verify the Ruby installation and executable path."},
	{"IPC-010", "TUI runtime error: <error>.", "Bubble Tea runtime exits with an error in main.go.", "Restart zeri and inspect /runtime-status."},
	{"IPC-011", "IPC connection lost: <error>.", "Yuumi client read loop loses the transport connection.", "Restart zeri and verify engine process health."},
	{"IPC-012", "bridge.start() failed: <error>.", "Engine bridge cannot bind or initialize the requested pipe transport.", "Ensure the pipe name is valid and not already in use, then retry."},
	{"AI-001", "AI endpoint unreachable at <url>.", "The $ai connectivity check (or a request) cannot reach the configured endpoint.", "Run /setup in $ai, start Ollama with 'ollama serve', or set '/set endpoint <url>'."},
	{"AI-002", "Failed to encode AI request payload.", "The chat request body cannot be serialized to JSON.", "Retry; if it persists, report the issue with the prompt used."},
	{"AI-003", "Failed to create AI request.", "The HTTP request to the AI endpoint cannot be constructed.", "Verify the endpoint URL with /setup, then retry."},
	{"AI-004", "AI request failed with status <code>.", "The endpoint responds with a non-2xx status (auth, model, or quota issue).", "Check the model name and API key with /setup; a 401/403 means the key is wrong or missing."},
	{"AI-005", "AI stream interrupted unexpectedly.", "The streaming response is cut off before completion.", "Check endpoint stability and retry; reduce prompt size if it recurs."},
}


func RenderErrorCatalog(filter string) string {
	needle := strings.ToUpper(strings.TrimSpace(filter))
	entries := make([]ErrorCatalogEntry, 0, len(errorCatalog))
	for _, entry := range errorCatalog {
		if needle == "" || strings.HasPrefix(entry.Code, needle) {
			entries = append(entries, entry)
		}
	}

	if len(entries) == 0 {
		return fmt.Sprintf("No error codes match %q.\nTry /errors (all codes) or a family like /errors ai.", strings.TrimSpace(filter))
	}

	sort.SliceStable(entries, func(i, j int) bool {
		return entries[i].Code < entries[j].Code
	})

	var b strings.Builder
	if needle == "" {
		b.WriteString("Zeri error catalog (")
		b.WriteString(fmt.Sprintf("%d codes", len(entries)))
		b.WriteString("). Filter with /errors <family> (example: /errors ai).\n")
	} else {
		b.WriteString(fmt.Sprintf("Zeri error catalog — %s (%d codes).\n", needle, len(entries)))
	}

	for _, entry := range entries {
		b.WriteString("\n")
		b.WriteString(entry.Code)
		b.WriteString(" — ")
		b.WriteString(entry.Message)
		b.WriteString("\nWhen: ")
		b.WriteString(entry.Trigger)
		b.WriteString("\nFix: ")
		b.WriteString(entry.Hint)
		b.WriteString("\n")
	}
	return strings.TrimRight(b.String(), "\n")
}
