# Manual Test Checklist

Manual checklist to validate complete REPL behavior: contexts, scripting, math, sandbox, bridge, startup, bootstrap, runtime center, script editor, clipboard, session management, autocomplete, and cross-platform compatibility.

---

## Build Prerequisites

```powershell
# C++ engine (Windows — CMake + Ninja + MSVC)
cmake --preset windows-debug
cmake --build build --config Debug

# Go TUI
cd ui
go build ./...
```

Expected: both builds complete with zero errors and zero warnings.

---

## Section A — Startup and Initialization

### T-01 — Normal startup, TUI opens immediately
- **Steps**: launch `zeri.exe` (or `./zeri` on Linux/macOS) with all runtimes present.
- **Expected**:
  - TUI renders immediately without blocking;
  - initialization panel appears with sequential phase labels: `Bootstrapping required runtimes...`, `Running environment checks...`, `Starting ZeriEngine...`, `Connecting bridge...`;
  - panel closes automatically and REPL becomes interactive;
  - global context prompt is visible; no crash.

### T-02 — Startup with missing non-fatal runtime
- **Steps**: disable or rename one optional runtime binary (e.g., `ruby`); launch Zeri.
- **Expected**:
  - initialization panel closes normally;
  - REPL becomes interactive;
  - a system warning message is displayed in the chat stream (not a raw log block);
  - detailed diagnostics are written to `dist/zeri-startup.log` with section `PREFLIGHT_NON_FATAL`.

### T-03 — Startup blocked by missing engine binary
- **Steps**: move or rename `ZeriEngine.exe` / `ZeriEngine` so it cannot be found; launch Zeri.
- **Expected**:
  - preflight emits `[ZERI][ENGINE_NOT_FOUND]` with hint `Ensure zeri and ZeriEngine are in the same directory.`;
  - startup panel remains visible with explicit error lines;
  - REPL does not open interactively.

### T-04 — First-run bootstrap installs missing runtimes
- **Steps**: simulate a clean profile where `bun`, `python3`, `luajit`, or `ruby` is absent; launch Zeri.
- **Expected**:
  - bootstrap manager attempts automatic installation via the platform package manager (`winget`/`choco`/`scoop` on Windows; `brew` on macOS; `apt-get`/`dnf`/`pacman`/`apk` on Linux);
  - after successful bootstrap, preflight passes without runtime-missing errors;
  - second launch does not repeat bootstrap for already-installed runtimes.

### T-05 — Bootstrap fallback when no package manager is available
- **Steps**: run on a host with no supported package manager and one missing base runtime.
- **Expected**:
  - startup does not crash;
  - diagnostics explicitly list missing runtimes and print the `installHint` from `runtime_manifest.json`;
  - startup log contains section `BOOTSTRAP` with error detail.

### T-06 — Manifest `minVersion` validation and `[ZERI][RUNTIME_OUTDATED]` diagnostic
- **Steps**:
  1. edit `runtime/runtime_manifest.json`; set `minVersion` for `bun` to `999.0.0`;
  2. launch Zeri;
  3. restore the original `minVersion` and relaunch.
- **Expected**:
  - with invalid requirement, startup log and system message contain `[ZERI][RUNTIME_OUTDATED]`;
  - after restoring, startup passes without version errors.

### T-07 — Bridge connection failure is reported explicitly
- **Steps**: prevent the yuumi named pipe `zeri-core` from being created (block the engine start externally); launch Zeri.
- **Expected**:
  - startup panel shows `[ZERI][ENGINE_NOT_FOUND]` or bridge-related error with hint;
  - REPL does not open interactively.

---

## Section B — Global Context

### T-08 — Global help output
- **Steps**: from `global`, run `/help`.
- **Expected**:
  - output includes sections: `Syntax`, `Global Commands`, `Contexts`;
  - `Contexts` lists `$code`, `$math`, `$sandbox`, `$setup`, `$global`, `$customCommand`;
  - no references to C++ or Rust as scripting languages;
  - all text is in English.

### T-09 — Context list from global
- **Steps**: from `global`, run `/context`.
- **Expected**:
  - list contains exactly: `$global`, `$code`, `$customCommand`, `$math`, `$sandbox`, `$setup`;
  - list does not include `$js`, `$ts`, `$lua`, `$python`, `$ruby`;
  - active context is marked.

### T-10 — Invalid direct language switch from global
- **Steps**: from `global`, type `$js`.
- **Expected**:
  - explicit error indicating switch is not allowed from `$global` to `$js`;
  - message suggests using `/context` or entering `$code` first.

### T-11 — Expression in global context is rejected
- **Steps**: from `global`, type `2+2`.
- **Expected**:
  - error `ExpressionInGlobal`;
  - message suggests switching to `$math`.

### T-12 — Session status and reset
- **Steps**: run `/status`, then `/reset`, then `/status` again.
- **Expected**:
  - counters are coherent before and after reset;
  - context is reported as `global` after reset.

### T-13 — Session save and load
- **Steps**:
  1. run `/save`; verify response;
  2. run `/load`; verify session is restored.
- **Expected**:
  - `/save` responds `Session saved successfully.` or explicit error with reason;
  - `/load` restores state without crash.

### T-14 — Clipboard: copy last and copy all
- **Steps**:
  1. produce at least two output messages;
  2. run `/copy last`;
  3. run `/copy all`.
- **Expected**:
  - `/copy last` places the latest non-user output in the system clipboard;
  - `/copy all` places the full visible chat history in the clipboard;
  - no crash on either command.

### T-15 — Engine restart
- **Steps**: run `restart` (bare command, no slash).
- **Expected**:
  - engine process is restarted and bridge reconnects;
  - REPL returns to interactive state;
  - status bar reflects reconnected state.

### T-16 — Runtime Center panel
- **Steps**: run `/runtime-status`.
- **Expected**:
  - Runtime Center panel opens with at minimum: `bun`, `python`, `luajit`, `ruby` entries;
  - each entry shows: name, check label, detected version or `not detected`, min version, status (`RUNTIME_OK` / `RUNTIME_MISSING` / `RUNTIME_OUTDATED`);
  - startup log path is shown if available;
  - panel closes cleanly on dismiss.

---

## Section D — Math Context

### T-19 — Math context entry and exit
- **Steps**: from `global`, run `$math`, then `/back`.
- **Expected**:
  - context switches to `$math` with confirmation;
  - `/back` returns to `global` without error.

### T-20 — Free-form math evaluation
- **Steps**: in `$math`, type `x = 42`, then `sin(pi/2)`, then `/vars`.
- **Expected**:
  - assignment is accepted;
  - `sin(pi/2)` evaluates to `1` (or `1.0`);
  - `/vars` lists `x = 42`.

### T-21 — User function definition
- **Steps**: in `$math`, run `/fn f(x)=x*sin(x)`, then `f(1.2)`.
- **Expected**:
  - response contains `[FunctionDefined]`;
  - `f(1.2)` evaluates to a valid numeric result.

### T-22 — Variable promotion
- **Steps**: in `$math`, run `x = 7`, then `/promote x session`.
- **Expected**: response is `[Promoted] x -> session` without errors.

### T-23 — Math function listing
- **Steps**: in `$math`, define two functions with `/fn`, then run `/fns`.
- **Expected**: both defined functions appear in the list.

---

## Section E — ScriptHub Context (`$code`)

### T-24 — `$code` help lists `$`-based language access only
- **Steps**: from `global`, enter `$code`, run `/help`.
- **Expected**:
  - `/help` does not list `/lua`, `/python`, `/js`, `/ts`, `/ruby` as slash commands;
  - languages are listed as `$lua`, `$python`, `$js`, `$ts`, `$ruby`;
  - no C++ or Rust scripting references.

### T-25 — Invalid slash command in `$code`
- **Steps**: in `$code`, run `/cpp`, then `/rust`.
- **Expected**: error `SCRIPTHUB_UNKNOWN_LANG` with hint `Use /help to list supported languages.` for both.

### T-26 — Context list inside `$code`
- **Steps**: in `$code`, run `/context`.
- **Expected**:
  - list contains only: `$global`, `$lua`, `$python`, `$js`, `$ts`, `$ruby`;
  - top-level contexts `$math`, `$sandbox`, `$setup` are not listed.

### T-27 — Language context entry from `$code`
- **Steps**: in `$code`, run `$js`.
- **Expected**: context switches to `$js` without error.

---

## Section F — Script Language Contexts (`$js`, `$ts`, `$lua`, `$python`, `$ruby`)

### T-28 — Script context help lists workflow commands
- **Steps**: enter `$js`, run `/help`.
- **Expected**:
  - commands listed include `/new`, `/edit`, `/show`, `/run`, `/list`, `/delete`;
  - no global-only commands appear as primary script commands.

### T-29 — Script `/new` opens full-screen editor
- **Steps**: in `$js`, run `/new sample`.
- **Expected**:
  - full-screen script editor opens with line numbers visible;
  - header shows language tag (`js`) and filename (`sample`);
  - status bar is visible at the bottom.

### T-30 — Script editor save menu and execution
- **Steps**: in the script editor (from T-29), type a JS expression, then press the save shortcut.
- **Expected**:
  - save menu appears with options: `Save and Execute`, `Save Only`, `Exit without Saving`;
  - selecting `Save and Execute` closes editor and runs the script;
  - output appears in the chat stream viewport, not below the input box.

### T-31 — `/show` renders inside chat stream
- **Steps**: in `$js`, run `/show sample` on an existing saved script.
- **Expected**:
  - code preview appears inside the chat history stream (viewport);
  - preview does not appear below the input box.

### T-32 — `/list` shows saved scripts
- **Steps**: after creating one or more scripts with `/new`, run `/list`.
- **Expected**: saved script names are listed with their language tags.

### T-33 — `/delete` removes a script
- **Steps**: in `$js`, run `/delete sample` for an existing script.
- **Expected**: confirmation of deletion; script no longer appears in `/list`.

### T-34 — `/run` executes last buffer or named script
- **Steps**: in `$js`, run `/run sample` for an existing saved script.
- **Expected**: script output appears in the chat stream; no crash.

---

## Section G — Sandbox Context (`$sandbox`)

### T-35 — Sandbox help and unknown command [AUTOMATED: ui/test/integration/f5_manual_automation_test.go]
- **Steps**: enter `$sandbox`, run `/help`, then `/unknown`.
- **Expected**:
  - `/help` lists: `/open`, `/set-ide`, `/watch`, `/list`, `/build`, `/run`;
  - `/unknown` produces error `SANDBOX_UNKNOWN` with the command name and hint `Use /help to see available commands.`

### T-36 — Sandbox watch status [AUTOMATED: ui/test/integration/f5_manual_automation_test.go]
- **Steps**: in `$sandbox`, run `/watch`.
- **Expected**:
  - output shows sandbox process status: `running` or `idle`;
  - no placeholder text is visible.

### T-37 — Sandbox IDE configuration [AUTOMATED: ui/test/integration/f5_manual_automation_test.go]
- **Steps**: in `$sandbox`, run `/set-ide code`, then `/open .`.
- **Expected**:
  - `/set-ide code` confirms IDE is set;
  - `/open .` dispatches open to VS Code or returns explicit error with hint about PATH or `sandbox::ide`.

### T-38 — Sandbox blocking external run [AUTOMATED: ui/test/integration/f5_manual_automation_test.go]
- **Steps**: in `$sandbox`, run `/run <path-to-script-or-external-binary>`.
- **Expected**:
  - streaming runtime output appears in the chat stream;
  - REPL is blocked until the process terminates;
  - final message includes success or explicit error with exit code.

---

## Section H — Setup Context

### T-39 — Setup wizard entry [MANUAL-ONLY: requires interactive SelectMenu/Confirm wizard input]
- **Steps**: from `global`, enter `$setup`, run `/start`.
- **Expected**: configuration wizard starts; no crash; guidance text is in English.

---

## Section I — Autocomplete and Completion Engine

### T-40 — `$`-context suggestions in global [MANUAL-ONLY: requires TUI autocomplete dropdown rendering]
- **Steps**: in `global`, type `$` and inspect the autocomplete dropdown.
- **Expected**:
  - suggestions include: `$code`, `$math`, `$sandbox`, `$setup`, `$customCommand`;
  - `$js`, `$ts`, `$lua`, `$python`, `$ruby` are not listed.

### T-41 — `/`-command suggestions in global [MANUAL-ONLY: requires TUI autocomplete dropdown rendering]
- **Steps**: in `global`, type `/` and inspect suggestions.
- **Expected**:
  - suggestions match global commands: `/help`, `/context`, `/back`, `/save`, `/load`, `/status`, `/reset`, `/clear`, `/exit`, `/set`, `/get`, `/copy last`, `/copy all`, `/runtime-status`.

### T-42 — Suggestions are reactive after context switch [MANUAL-ONLY: requires interactive autocomplete state transitions]
- **Steps**:
  1. in `global`, type `$` and note suggestions;
  2. switch to `$code`;
  3. type `$` and note suggestions;
  4. switch to `$js`;
  5. type `/` and note suggestions.
- **Expected**:
  - in `$code`, `$` suggestions are: `$lua`, `$python`, `$js`, `$ts`, `$ruby`;
  - in `$js`, `/` suggestions match script workflow commands: `/new`, `/edit`, `/show`, `/run`, `/list`, `/delete`;
  - no stale global-only suggestions appear after context switch.

### T-43 — Catalog alignment: synopsis update propagates to both engine and TUI [MANUAL-ONLY: requires rebuilding plus visual TUI autocomplete inspection]
- **Steps**:
  1. update the synopsis of one command in `help/help_catalog.json`;
  2. rebuild both engine and TUI;
  3. run `/help` in the matching context;
  4. inspect autocomplete synopsis for the same command.
- **Expected**:
  - `/help` output and autocomplete synopsis both reflect the updated text;
  - no hardcoded stale synopsis remains.

---

## Section J — UI Viewport and Input

### T-44 — Mouse wheel scrolls chat, not input history [MANUAL-ONLY: requires mouse input and viewport rendering]
- **Steps**: produce enough messages to overflow the viewport; scroll mouse wheel up and down while input is focused.
- **Expected**:
  - chat viewport scrolls;
  - input value does not change to previous history entries.

### T-45 — Input history navigation with keyboard [MANUAL-ONLY: requires keyboard navigation in the interactive TUI]
- **Steps**: submit several commands; press `Up` arrow when the cursor is on the first line of the input.
- **Expected**:
  - previous command appears in input;
  - `Down` arrow advances forward;
  - `Ctrl+Up` / `Ctrl+Down` also navigate history.

### T-46 — Status bar reflects active context and connection state [MANUAL-ONLY: requires live status bar rendering checks]
- **Steps**: switch through `$math`, `$code`, `$js`, then `/back` twice.
- **Expected**:
  - status bar updates to show the active context tag at each step;
  - connection state indicator reflects `connected` while bridge is active.

---

## Section K — Cross-Platform Compatibility

### T-47 — Named pipe path resolution (Windows vs. Unix socket) [MANUAL-ONLY: requires multi-platform runtime verification]
- **Steps**: run Zeri on Windows; inspect startup log for pipe path; repeat on Linux or macOS.
- **Expected**:
  - on Windows, named pipe is used (`\\.\pipe\zeri-core`);
  - on Linux/macOS, Unix domain socket is resolved via `socketPathFromPipe` to a `.sock` file in `os.TempDir()`;
  - bridge connects successfully on both platforms.

### T-48 — Engine binary name resolved per platform [MANUAL-ONLY: requires multi-platform process launch verification]
- **Steps**: verify that on Windows `ZeriEngine.exe` is used and on Linux/macOS `ZeriEngine` (no extension).
- **Expected**:
  - `main.go` selects correct binary name via `runtime.GOOS`;
  - `ZERI_ENGINE_PATH` environment variable overrides both cases.

### T-49 — Bootstrap package manager selection per platform [MANUAL-ONLY: requires OS-specific package-manager environments]
- **Steps**: trigger bootstrap on Windows, macOS, and Linux (or simulate via `runtime.GOOS` inspection).
- **Expected**:
  - Windows uses `winget`, `choco`, or `scoop` (first available);
  - macOS uses `brew`;
  - Linux uses `apt-get`, `dnf`, `yum`, `pacman`, `zypper`, or `apk` (first available);
  - installer order matches `runtime_manifest.json` `installers` ordering.

---

## Section L — Output and Message Style

### T-50 — All user-facing messages are in English [AUTOMATED: ui/test/integration/f5_manual_automation_test.go]
- **Steps**: run `/help`, `/context`, `/status`, switch contexts, trigger errors.
- **Expected**: zero Italian strings appear in any user-facing output.

### T-51 — Error format consistency [AUTOMATED: ui/test/integration/f5_manual_automation_test.go]
- **Steps**: trigger `[ZERI][ENGINE_NOT_FOUND]`, `[ZERI][RUNTIME_MISSING]`, `[ZERI][RUNTIME_OUTDATED]`, `[ZERI][BOOTSTRAP_MANIFEST_INVALID]`.
- **Expected**: all errors follow format `[ZERI][CODE] Check: <name>\n <message>\n Hint: <hint>`.

### T-52 — No C++/Rust scripting language references in any output [AUTOMATED: ui/test/integration/f5_manual_automation_test.go]
- **Steps**: run `/help` in `global`, `$code`, `$sandbox`; inspect autocomplete for all contexts.
- **Expected**: zero references to C++ or Rust as scripting languages in any output, help text, or completion entry.

---

## Exit Criteria

- All tests **T-01 through T-52** pass without crashes on the target platform.
- C++ engine builds successfully with `cmake --build` (Ninja, MSVC, `/W4 /WX`).
- Go TUI builds successfully with `go build ./...`.
- No Italian strings remain in any user-facing output.
- No C++/Rust scripting language references remain in output, help, or autocomplete.
- `dist/zeri-startup.log` is created on startup and contains structured diagnostic sections.
- Runtime Center (`/runtime-status`) loads and renders without crash when `runtime_manifest.json` is valid.
