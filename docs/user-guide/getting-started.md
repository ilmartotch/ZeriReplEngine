# Getting started

This page is enough to run your first Python script in Zeri on a clean machine.

## 1. Install Zeri

Use one installer command:

```bash
brew install ilmartotch/zeri/zeri
```

```powershell
scoop bucket add zeri https://github.com/ilmartotch/scoop-zeri; scoop install zeri
```

Expected result:

```text
The zeri command is available in a new terminal session.
```

## 2. Start Zeri

Run:

```bash
zeri
```

Expected output (startup stream includes these lines):

```text
Global Context active. Type /help for available commands.
```

## 3. Enter ScriptHub (`$code`)

Run:

```text
$code
```

Expected output:

```text
Code context active. Use /context to list reachable contexts, then switch with $<context>.
```

## 4. Enter Python context

Run:

```text
$python
```

Expected output:

```text
Python context active. Use /new <name>, /run, /list, /edit <name>, /show <name>, /delete <name>.
```

## 5. Run your first script line

Run:

```text
print("hello")
```

Expected output:

```text
hello
```

## 6. Save and run a named script

Run:

```text
/new hello
```

Expected output:

```text
Python editor opened for script: hello
```

In the editor, type:

```text
print("hello from file")
```

Then save with:

```text
/save
```

Expected output after save:

```text
Script saved: hello
```

Back in Python context, run:

```text
/run hello
```

Expected output:

```text
hello from file
```

## 7. Where Zeri keeps its state

Zeri records first-run state in a single canonical file, `location.json`, inside
the per-user config home:

```text
Windows: %APPDATA%\Zeri\location.json
macOS: ~/Library/Application Support/zeri/location.json
Linux: $XDG_CONFIG_HOME/zeri/location.json (or ~/.config/zeri/location.json)
```

That file holds both the chosen data root (`data_root`) and whether onboarding
has been completed (`onboarding_completed`). Your scripts and sessions live under
the data root and are never stored in `location.json`.

To replay onboarding from scratch, run:

```text
zeri --reset-onboarding
```

This clears only the `onboarding_completed` flag and leaves your data untouched.
