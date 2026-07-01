# Contributing to Zeri

This document defines mandatory coding and review rules for this repository.  
Every pull request must follow this guide.

## Setup instructions

## Build dependencies

1. Go 1.25 or newer
2. CMake 3.28 or newer
3. C++ toolchain: MSVC 2022, GCC 13+, or Apple Clang. On Linux, Clang with
   libstdc++ requires version 19+ — older Clang reports `__cpp_concepts = 201907L`
   and cannot see C++23 `std::expected` (see "Continuous integration").
4. Git
5. vcpkg (set `VCPKG_ROOT` or clone it in repository root)

## Build commands

**Windows (PowerShell):**

```powershell
.\build.ps1 -Config Release
```

**Linux/macOS (bash):**

```bash
./build.sh Release
```

## Go validation (UI module)

```bash
cd ui
go test ./...
go vet ./...
```

## Catalog-driven metadata (commands/errors/languages/bridge types)

Descriptive metadata is single-source and embedded at build time for both
engine and TUI. Edit only these files:

- `ui/pkg/catalog/data/commands_catalog.json`
- `ui/pkg/catalog/data/errors_catalog.json`
- `ui/pkg/catalog/data/languages_catalog.json`
- `ui/pkg/catalog/data/bridge_types_catalog.json`

Rules:

1. Keep `version` as a positive integer.
2. Command scopes must reference existing context IDs.
3. Language `context` values must reference existing context IDs.
4. Bridge type IDs must be unique and consumed by name via protocol helpers.

Do not add parallel hardcoded registries in Go/C++ when a catalog entry can
express the same metadata.

## Continuous integration

CI (`.github/workflows/ci.yml`) runs on every push to `main`/`dev` and on pull
requests to `main`:

- **Quality checks** — fast static gates (see "Hard rules" below). The marker
  check (`TODO/FIXME/HACK/XXX`) is blocking on `main` and pull requests only.
- **Build and smoke** — full build plus startup smoke tests on the release
  matrix: Windows (MSVC), Linux (GCC), macOS (Apple Clang).
- **Static analysis** — `cppcheck` on Linux.
- **Sanitizers** — ASan/UBSan and TSan on Linux, built with **GCC**.

### Why the sanitizer job uses GCC, not Clang

The codebase uses C++23 `std::expected`. With libstdc++, the `<expected>`
header only defines `std::expected` when `__cpp_concepts >= 202002L`. The Clang
shipped with Ubuntu 24.04 (Clang 18) reports `__cpp_concepts = 201907L`, so
`std::expected` is silently excluded and the build fails — independent of the
libstdc++ version. GCC reports `202002L` and compiles the C++23 sources. Since
Clang + libstdc++ on Linux is not a release target, the sanitizer job uses GCC,
whose ASan/UBSan/TSan are equivalent for the memory and threading issues these
tests target. Clang on Linux with libstdc++ would require Clang 19+; Apple Clang
(libc++) and MSVC are unaffected.

## Hard rules (blocking)

## 1) No duplicate utility functions

Use canonical utilities only:
- `src/Core/Include/StringUtils.h`
- `src/Engines/Include/ExecutorUtils.h`
- `src/Engines/Include/ContextUtils.h`

Local copies are a blocking error.

**Correct:**

```cpp
#include "StringUtils.h"
auto normalized = zeri::core::Trim(input);
```

**Wrong:**

```cpp
static std::string Trim(const std::string& v) {
    // local copy of shared utility: forbidden
    return v;
}
```

## 2) English only

All comments, string literals, docs, and commit messages must be English.

**Correct:**

```cpp
throw std::runtime_error("Invalid command syntax.");
```

**Wrong:**

```cpp
throw std::runtime_error("Sintassi comando non valida.");
```

## 3) Comment style

Allowed:
- End-of-file `/* */` documentation blocks
- Temporary inline marker only: `// [REVIEW-PENDING]: reason`

Forbidden:
- Generic inline `// ...` comments
- `// TODO`, `// FIXME`, `// HACK`, `// XXX`

**Correct:**

```cpp
ProcessResult result = RunCommand(request);
// [REVIEW-PENDING]: waiting for task F2-412 to finalize cancellation semantics
```

**Wrong:**

```cpp
// TODO: improve this later
ProcessResult result = RunCommand(request); // temporary patch
```

## 4) No vertical padding in Go

Use `name type` with single spacing only.  
Alignment padding is forbidden.

**Correct:**

```go
type Config struct {
	Path string
	Mode string
}
```

**Wrong:**

```go
type Config struct {
	Path     string
	Mode     string
}
```

## 5) No placeholder implementations

Empty bodies are forbidden unless they are explicitly marked with `// [REVIEW-PENDING]` and linked to a task.

**Correct:**

```cpp
void ApplyPolicy() {
    // [REVIEW-PENDING]: blocked by F2-501
}
```

**Wrong:**

```cpp
void ApplyPolicy() {
}
```

## 6) No magic numbers

Numeric constants must live in dedicated constants files, not inline logic.

**Correct:**

```cpp
#include "EngineConstants.h"
if (retries > kMaxRetryCount) {
    return false;
}
```

**Wrong:**

```cpp
if (retries > 3) {
    return false;
}
```

## 7) End-of-file `/* */` block required

Every modified `.cpp` and `.h` file must end with a closing documentation block.

**Correct:**

```cpp
return Execute(context);
/*
Updated execution path to use ContextUtils canonical helpers.
Validated against existing IPC behavior.
*/
```

**Wrong:**

```cpp
return Execute(context);
```

## Pull request checklist

Before opening or updating a PR, confirm all items below:

- [ ] I used canonical utility headers and did not duplicate shared functions.
- [ ] All new text (code strings, comments, docs, commit messages) is English.
- [ ] I did not add forbidden inline comments (`TODO/FIXME/HACK/XXX`).
- [ ] Go code does not use vertical alignment padding.
- [ ] I did not leave placeholder implementations without a valid `[REVIEW-PENDING]` marker and linked task.
- [ ] Numeric constants were moved to constants files.
- [ ] Every modified `.cpp` and `.h` file ends with a final `/* */` documentation block.
- [ ] CI quality checks pass.
