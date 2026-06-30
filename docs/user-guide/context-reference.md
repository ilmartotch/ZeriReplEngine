# Context reference

All contexts below are currently implemented and reachable from the REPL.

## `$global`

- **Entry command**: start Zeri or use `$global`
- **Prompt**: `zeri>`

| Command | Description |
| --- | --- |
| `/help` | Show help for the active context |
| `restart` | Restart and reconnect the engine after a disconnection |
| `/runtime-status` | Open Runtime Center with runtime diagnostics |
| `/settings` | Open the configuration hub (IDE, data location pointer, AI settings) |
| `/copy last` | Copy the latest non-user output message |
| `/copy all` | Copy the full visible message history |
| `/context` | List reachable contexts |
| `/back` | Return to previous context |
| `/save` | Save session state to disk |
| `/load` | Load a saved session from disk |
| `/status` | Show engine diagnostics and persistence paths |
| `/reset` | Reset the current session |
| `/bug report` | Show bug-report instructions |
| `/bug snapshot` | Create a diagnostic snapshot file |
| `/clear` | Clear chat history |
| `/exit` | Exit the REPL |
| `/set` | Store a typed variable in scope (`--number`, `--string`, `--bool`) |
| `/get` | Read a variable from the current scope |

**Example**

```text
/context
```

## `$code`

- **Entry command**: `$code`
- **Prompt**: `zeri::code>>`

| Command | Description |
| --- | --- |
| `/help` | Show ScriptHub-specific help |
| `/context` | List reachable contexts from `$code` |
| `/back` | Return to `$global` |

**Example**

```text
$python
```

## `$sandbox`

- **Entry command**: `$sandbox`
- **Prompt**: `zeri::sandbox>>`

| Command | Description |
| --- | --- |
| `/open [file]` | Open a file/path in the configured IDE |
| `/watch` | Show current sandbox process status |
| `/list` | List modules from the modules directory |
| `/build <moduleName>` | Build a module with CMake |
| `/run <target> [args...] [--cwd <path>]` | Run a module target or external executable/script |

**Example**

```text
/watch
```

## `$math`

- **Entry command**: `$math`
- **Prompt**: `zeri::math>`

| Command | Description |
| --- | --- |
| `/eval <expr>` | Evaluate an expression explicitly |
| `/fn <sig> = <body>` | Define a function |
| `/vars` | List variables in current scope |
| `/fns` | List defined and inherited functions |
| `/promote <var> <scope>` | Promote variable scope (`session`, `global`, `persisted`) |
| `/logic <op> <v1> <v2>` or `/logic <v1> <op> <v2>` | Boolean logic helper (`and`, `or`, `xor`, `&&`, `\|\|`, `^`) |

**Example**

```text
/fn f(x)=x*sin(x)
```

## `$customCommand`

- **Entry command**: `$customCommand`
- **Prompt**: `zeri::custom>`

| Command | Description |
| --- | --- |
| `/define <name> "<body>" [--context <ctx>]` | Define an executable macro (global by default, or bind to a context) |
| `/list` | List registered custom commands with scope |
| `/run <name>` | Execute a saved custom command (fail-fast chain) |
| `/show <name>` | Show the resolved custom command body and scope |
| `/delete <name>` | Delete a custom command (interactive disambiguation when names collide across scopes) |

**Example**

```text
/define build "/save ; /run"
/define calc7 "/eval 7" --context math
```

Rules:
- Body steps are split by `;` and executed in order with fail-fast semantics.
- Only explicit slash commands are allowed inside the body.
- `;` inside quotes (or escaped as `\;`) is treated as literal text, not a separator.
- Commands are invokable by name from prompts: resolution order is built-in > context-bound > global.
