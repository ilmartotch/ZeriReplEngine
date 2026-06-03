# Zeri

A modular, language-aware REPL with a rich terminal UI — built for developers who live in the terminal.

```
$ zeri
```

![Zeri TUI](docs/screenshot.png)

## Features

- **Context-aware REPL** — switch between Chat, JavaScript, and Python modes on the fly
- **Rich TUI** — full-screen terminal interface powered by [Bubble Tea](https://github.com/charmbracelet/bubbletea) and [Lipgloss](https://github.com/charmbracelet/lipgloss)
- **Wizard UI** — guided setup and interactive prompts from within the engine
- **Extensible** — Lua scripting support and a modular context/executor architecture
- **Two-process design** — headless C++ engine + Go TUI communicating over IPC transport (Windows Named Pipes, Unix Domain Sockets on Linux/macOS)

## Installation

### Windows

Install with Scoop:

```powershell
scoop bucket add zeri https://github.com/ilmartotch/scoop-zeri && scoop install zeri
```

Open a new terminal and type `zeri` to start.

### macOS / Linux

Install with Homebrew:

```bash
brew install ilmartotch/zeri/zeri
```

Open a new terminal and type `zeri` to start.

---

> **First time?** Open a **new** terminal window after installation — the current session may not pick up PATH changes automatically.

## Quick Start

```
$ zeri
```

| Command | Action |
|---------|--------|
| `$js` | Switch to JavaScript mode |
| `$py` | Switch to Python mode |
| `$clear` | Clear the screen |
| `$exit` | Exit Zeri |
| `/help` | Show available commands |
| `Ctrl+B` | Toggle sidebar |
| `Ctrl+C` | Quit |

## Building from Source

### Requirements

| Tool | Version | Notes |
|------|---------|-------|
| Go | 1.25+ | [go.dev/dl](https://go.dev/dl/) |
| CMake | 3.28+ | [cmake.org](https://cmake.org/download/) |
| MSVC / GCC / Clang | C++23 | VS 2022 / GCC 14+ / Clang 17+ |
| vcpkg | baseline-pinned | [vcpkg.io](https://vcpkg.io) |

Set `VCPKG_ROOT` or bootstrap vcpkg locally:

```bash
git clone https://github.com/microsoft/vcpkg vcpkg
./vcpkg/bootstrap-vcpkg.sh   # macOS / Linux
.\vcpkg\bootstrap-vcpkg.bat  # Windows
```

If `VCPKG_ROOT` is not set, build/dev scripts automatically try a local `./vcpkg` folder.

### Build

**Windows (PowerShell):**
```powershell
.\build.ps1
```

**macOS / Linux:**
```bash
./build.sh
```

Output is in `dist/`:
```
dist/
  zeri.exe        (or zeri)
  ZeriEngine.exe  (or ZeriEngine)
  runtime/
```

### Dev Workflow (one command)

```powershell
.\dev.ps1   # Windows
./dev.sh    # macOS / Linux
```

Builds `ZeriEngine` in Debug, sets `ZERI_ENGINE_PATH`, and launches the TUI via `go run`.

`ZERI_ENGINE_PATH` can be overridden manually if you want to test a custom engine binary:

```powershell
$env:ZERI_ENGINE_PATH = "D:\path\to\ZeriEngine.exe"
go run ./ui/cmd/zeri-tui/
```

## Architecture

Zeri is a two-process system:

```
┌─────────────────────────────────────────────┐
│  zeri  (Go)                                 │
│  Bubble Tea TUI · Lipgloss styling          │
│  Launches and orchestrates ZeriEngine       │
└──────────────────┬──────────────────────────┘
                   │  Named Pipe (Windows) / Unix Domain Socket (Unix)
                   │  JSON over binary framing
┌──────────────────▼──────────────────────────┐
│  ZeriEngine  (C++)                          │
│  REPL core · Context manager · Lua engine   │
│  Headless — no UI, no stdin, no stdout      │
└─────────────────────────────────────────────┘
```

The TUI frontend is provided by **Yuumi**, an internal Go package (`ui/`) that handles the IPC bridge and Bubble Tea lifecycle.

## Platform Support

| Platform | Status |
|----------|--------|
| Windows x64 | Supported |
| Linux x64 | Supported |
| macOS arm64 | Supported |

## Dependencies

### C++ (ZeriEngine)
- [asio](https://think-async.com/Asio/) — async networking / IPC transport
- [nlohmann/json](https://github.com/nlohmann/json) — JSON protocol
- [Lua 5.4](https://www.lua.org/) — scripting engine
- [exprtk](https://github.com/ArashPartow/exprtk) — expression evaluation

### Go (TUI)
- [Bubble Tea](https://github.com/charmbracelet/bubbletea) — TUI framework
- [Lipgloss](https://github.com/charmbracelet/lipgloss) — terminal styling
- [Bubbles](https://github.com/charmbracelet/bubbles) — UI components
- [go-winio](https://github.com/Microsoft/go-winio) — Windows Named Pipe transport

## Build Reproducibility

The project uses:

- `vcpkg-configuration.json` with a fixed `baseline` commit
- version constraints in `vcpkg.json`
- `vcpkg-lock.json` as repository-level lock metadata

For deterministic CI, keep baseline + dependency constraints aligned when updating libraries.

## CI/CD Guidance (recommended rollout)

CI matrix for each push/PR:
- `windows-latest` (x64)
- `ubuntu-latest` (x64)
- `macos-latest` (arm64)

Pipeline stages:
- Configure (`cmake --fresh ... -DVCPKG_TARGET_TRIPLET=...`)
- Build C++ engine
- Build Go TUI
- Smoke test (`zeri --version` and simple IPC connect test)
- Package (`dist/` archive per OS/arch)

Release strategy:
- Tag-driven release (e.g. `v1.2.0`)
- Upload OS-specific artifacts:
  - `zeri-windows-amd64.zip`
  - `zeri-linux-amd64.tar.gz`
  - `zeri-macos-arm64.tar.gz`

Windows distribution:
- Keep `build.ps1` copying vcpkg runtime DLLs
- Keep `build.ps1` copying MSVC runtime DLLs from VS redist (if available)
- Keep `preflight_windows.go` runtime checks as guard rail.

## License

MIT — see [LICENSE](LICENSE)
