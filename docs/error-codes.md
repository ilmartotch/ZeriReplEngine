# Error codes

| Code | Message | Trigger condition | Hint |
| --- | --- | --- | --- |
| CONTEXT-001 | No active context is available. | Context stack is unexpectedly empty when switching to `$global`. | Run `/reset` to restore the global context. |
| CONTEXT-002 | Context switch is not allowed from `$from` to `$to`. | User requests a context that is not reachable from the current context. | Run `/context` to list reachable targets. |
| CONTEXT-003 | Unknown context: `<name>`. | Requested context name is not implemented. | Run `/context` and use one listed target. |
| CONTEXT-004 | No active context is available. | Command/expression dispatch occurs while no active context exists. | Run `/reset` to restore the global context. |
| CONTEXT-005 | Help catalog is unavailable, `/help` output may be incomplete. | `help/help_catalog.json` missing/unreadable at runtime. | Package `help/help_catalog.json` next to the executable. |
| CONTEXT-006 | Context warning forwarded as error output. | Warning message is emitted on error channel without explicit ZERI code. | Run `/help` in the current context and retry the command. |
| CONTEXT-007 | Typed execution error forwarded as standardized error output. | `ExecutionError` result reaches output handler without a ZERI-prefixed code. | Review the error code and retry with documented syntax. |
| CONTEXT-008 | Script editor executor is unavailable. | `/run` is invoked inside editor without executor instance. | Re-enter language context and retry `/run`. |
| CONTEXT-009 | `/save` requires a script name associated with the editor context. | `/save` is used in editor without bound script name. | Open editor with `/new <name>` or `/edit <name>`. |
| SESSION-001 | Failed to save session: `<error>`. | `/save` command fails while writing session state. | Verify write permission for sessions path shown by `/status`. |
| SESSION-002 | Failed to generate bug snapshot: `<error>`. | `/bug snapshot` file generation fails. | Verify project directory file permissions and retry `/bug snapshot`. |
| SESSION-003 | Previous session file was corrupted or could not be loaded. | Engine startup loads corrupted session state. | Run `/save` to write a fresh session snapshot. |
| SESSION-004 | Failed to save session on shutdown: `<error>`. | Automatic save during engine shutdown fails. | Ensure sessions path is writable (`/status`). |
| SESSION-010 | Storage initialization failed: `<error>`. | Go TUI fails to create/resolve local storage directories at startup. | Verify write permissions for user data directories. |
| SESSION-011 | Unable to resolve executable path: `<error>`. | Go TUI cannot resolve its own executable path. | Reinstall zeri or launch it from a valid installation directory. |
| CLI-001 | Unknown option: `<option>`. | Unsupported CLI option passed to `zeri`. | Use supported options: `--no-onboarding`, `--profile-startup`, `--exit-after-ready`. |
| CLI-002 | Missing required `--yuumi-pipe <name>` argument. | `zeri-engine` is started directly without IPC pipe argument. | Launch through `zeri` (TUI) or pass `--yuumi-pipe <name>` explicitly. |
| PARSE-001 | Empty system command. | `!` is used without an actual shell command. | Use `!<command>` (example: `!echo hello`). |
| PARSE-002 | `<parse message>`. | Meta parser rejects malformed input (for example unclosed quote). | Fix syntax and retry. |
| PARSE-003 | Unknown command. | Pipe operator `|` or unsupported token pattern is detected in command path. | Run `/help` to see available commands. |
| PARSE-004 | Invalid context switch syntax. | Context switch includes unsupported flags/extra args. | Use `$<context>` without flags/extra arguments. |
| PARSE-005 | Unrecognized input type. | Dispatcher receives unsupported input type enum. | Run `/help` to review supported input forms. |
| RUNTIME-001 | Failed to execute system command: `<command>`. | System shell process cannot be started. | Confirm command exists in `PATH` and retry. |
| RUNTIME-002 | System command exited with code: `<code>`. | System command returns non-zero exit status. | Inspect command output and fix failing command. |
| RUNTIME-003 | Startup diagnostic: `<line>`. | Startup diagnostics contain runtime/startup issues. | Fix reported dependency or runtime configuration. |
| RUNTIME-004 | System prerequisite check failed. | System guard reports missing required tools. | Install missing prerequisites listed below the error. |
| RUNTIME-005 | Sandbox process stderr: `<chunk>`. | Sandbox external process writes to stderr. | Review runtime output and fix target/process configuration. |
| RUNTIME-006 | Unhandled exception: `<error>`. | `zeri-engine` exits through top-level `std::exception` handler. | Inspect engine logs and rerun the same command to reproduce. |
| RUNTIME-007 | Unhandled non-standard exception. | `zeri-engine` exits through top-level catch-all handler. | Inspect engine logs and report the crash signature. |
| RUNTIME-020 | JS/TS runtime stderr: `<stderr>`. | JS/TS sidecar execution returns stderr content. | Inspect script output and runtime dependencies. |
| RUNTIME-021 | JS/TS sidecar launch failed, falling back to one-shot execution. | JS/TS sidecar process cannot start. | Verify Bun runtime and bootstrap scripts. |
| RUNTIME-022 | JS/TS one-shot stderr: `<line>`. | JS/TS one-shot process writes stderr lines. | Inspect script output and runtime dependencies. |
| RUNTIME-023 | Lua runtime stderr: `<stderr>`. | Lua sidecar execution returns stderr content. | Inspect Lua output and runtime dependencies. |
| RUNTIME-024 | Lua one-shot stderr: `<line>`. | Lua one-shot process writes stderr lines. | Inspect Lua output and runtime dependencies. |
| RUNTIME-025 | Lua sidecar bootstrap not found, falling back to one-shot execution. | `runtime/bootstrap_lua.lua` is missing. | Ensure `runtime/bootstrap_lua.lua` is packaged. |
| RUNTIME-026 | Lua sidecar launch failed, falling back to one-shot execution. | Lua sidecar process cannot start. | Verify `luajit` installation and executable path. |
| RUNTIME-027 | Python one-shot stderr: `<line>`. | Python one-shot process writes stderr lines. | Inspect Python output and runtime dependencies. |
| RUNTIME-028 | Python runtime stderr: `<stderr>`. | Python sidecar execution returns stderr content. | Inspect Python output and runtime dependencies. |
| RUNTIME-029 | Python sidecar launch failed, falling back to one-shot execution. | Python sidecar process cannot start. | Verify Python installation and bootstrap script availability. |
| RUNTIME-030 | Ruby runtime stderr: `<stderr>`. | Ruby sidecar execution returns stderr content. | Inspect Ruby output and runtime dependencies. |
| RUNTIME-031 | Ruby one-shot stderr: `<line>`. | Ruby one-shot process writes stderr lines. | Inspect Ruby output and runtime dependencies. |
| RUNTIME-032 | Ruby sidecar bootstrap not found, falling back to one-shot execution. | `runtime/bootstrap_ruby.rb` is missing. | Ensure `runtime/bootstrap_ruby.rb` is packaged. |
| RUNTIME-033 | Ruby sidecar launch failed, falling back to one-shot execution. | Ruby sidecar process cannot start. | Verify Ruby installation and executable path. |
| IPC-010 | TUI runtime error: `<error>`. | Bubble Tea runtime exits with error in `main.go`. | Restart zeri and inspect `/runtime-status`. |
| IPC-011 | IPC connection lost: `<error>`. | Yuumi client read loop loses transport connection. | Restart zeri and verify engine process health. |
| IPC-012 | bridge.start() failed: `<error>`. | Engine bridge cannot bind or initialize the requested pipe transport. | Ensure the pipe name is valid and not already in use, then retry. |
