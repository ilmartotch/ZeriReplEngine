# Zeri

Zeri is a multi-context developer REPL that combines scripting, math, sandbox execution, and setup workflows in one terminal UI. It is for developers who want to switch quickly between execution contexts without leaving the same interactive session. Its primary differentiator is cross-language shared state: set data in one language and read it in another in the same runtime, without files or external buses.

[![CI](https://github.com/ilmartotch/ZeriReplEngine/actions/workflows/ci.yml/badge.svg)](https://github.com/ilmartotch/ZeriReplEngine/actions/workflows/ci.yml) ![Version](https://img.shields.io/badge/version-1.0.0-blue) ![Windows](https://img.shields.io/badge/platform-Windows-0078D6) ![Linux](https://img.shields.io/badge/platform-Linux-FCC624) ![macOS](https://img.shields.io/badge/platform-macOS-999999)

## Install

```bash
brew install ilmartotch/zeri/zeri
```

```powershell
scoop bucket add zeri https://github.com/ilmartotch/scoop-zeri; scoop install zeri
```

## Quick start

```text
1. zeri
2. $code
3. $python
4. print("hello")
5. /new hello  (then in editor: write code and run /save)
```

## Custom command macros

`$customCommand` definitions are executable macros:

- `/define <name> "<body>" [--context <ctx>]` stores one or more slash commands
- body steps use `;` and run in fail-fast order
- commands are invokable by name with precedence `built-in > context-bound > global`

## Catalog architecture

Command metadata, error codes, language definitions, and IPC type names are
single-source catalogs in `ui/pkg/catalog/data/*.json`, embedded into both
engine and TUI at build time.

## Full documentation

See `docs/user-guide/` for the complete user guide:

- `docs/user-guide/getting-started.md`
- `docs/user-guide/context-reference.md`
- `docs/user-guide/script-editor.md`
- `docs/user-guide/sandbox.md`
- `docs/cross-language.md`

## License

MIT — see [LICENSE](LICENSE)
