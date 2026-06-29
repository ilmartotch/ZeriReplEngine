# Sandbox context

Sandbox (`$sandbox`) is the context for module operations and external target execution.

## Enter sandbox

```text
$sandbox
```

Expected output:

```text
Sandbox environment active - paste a file path to execute it, or type /help for commands.
```

## Core commands

| Command | Purpose |
| --- | --- |
| `/list` | List modules |
| `/build <moduleName>` | Build a module |
| `/run <moduleName|filePath> [args...] [--cwd <path>]` | Run a module or external target |
| `/open [file]` | Open path in configured IDE |
| `/watch` | Show sandbox process monitor state |

## Typical workflow

1. Configure IDE:
   ```text
   /settings ide code
   ```
2. Inspect modules:
   ```text
   /list
   ```
3. Build a module:
   ```text
   /build mymodule
   ```
4. Run a target:
   ```text
   /run mymodule
   ```

During run, status messages are emitted:

```text
Sandbox process status: running
Sandbox process status: idle
```
