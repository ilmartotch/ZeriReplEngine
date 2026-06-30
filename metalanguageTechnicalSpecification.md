# Meta-Language Technical Specification

## Overview

Zeri uses a context-driven REPL meta-language. Each input line is parsed and dispatched by prefix and active context.

## Input categories

1. **Slash command**: `/...`
2. **Context switch**: `$...`
3. **System command**: `!...`
4. **Expression**: any non-prefixed line
5. **Comment**: `#` starts an inline comment outside quoted text

## Prefix semantics

- `/` invokes a command in the current context.
- `$` switches to a reachable context.
- `!` executes a system shell command.
- `#` strips the rest of the line unless inside quotes.

## Tokenization and parsing rules

- Leading/trailing whitespace is trimmed.
- Commands and context names are case-insensitive.
- Quoted strings are supported.
- `--flag` syntax is supported; flags default to `true` when no value is provided.
- Parse failures are explicit (for example unclosed quoted string).

## Context model

- `$global`
- `$code`
- `$customCommand`
- `$math`
- `$sandbox`
- Script contexts reachable from `$code`: `$lua`, `$python`, `$js`, `$ts`, `$ruby`

## Command execution model

- Context-specific command sets are enforced.
- Global commands are always available where configured (`/help`, `/context`, `/back`, `/save`, `/load`, `/status`, `/reset`, `/exit`, `/set`, `/get`, `/bug`, `/runtime-status`, `/copy`, `/clear`, `restart`).
- Script contexts support `/new`, `/edit`, `/show`, `/run`, `/list`, `/delete`.
- `$math` supports free-form expressions and `/eval`, `/fn`, `/vars`, `/fns`, `/promote`, `/logic`.
- `$sandbox` supports `/open`, `/watch`, `/list`, `/build`, `/run`.
- `$customCommand` supports `/define`, `/list`, `/run`, `/show`, `/delete`.

## Variables and scope

- Variables are dynamically typed.
- Scopes in runtime: local, session, global, persisted.
- `/set` uses explicit type flags (`--number`, `--string`, `--bool`).
- `/promote` in `$math` promotes local variables to `session`, `global`, or `persisted`.

## Error model

- Parser and command errors are explicit.
- Execution errors include code, message, optional context, and optional hints.
- Output stream differentiates output/info/success/error channels.


## Planned

The following are intentionally not part of the current implementation and are reserved for future revisions:

- Script metadata headers interpreted by the REPL
- Plugin hooks that modify parser behavior
- Named argument syntax in function calls at parser level
- Operator overloading
- Command chaining or any pipeline syntax
