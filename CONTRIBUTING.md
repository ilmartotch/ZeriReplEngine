# Contributing to Zeri

This document defines mandatory coding and review rules for this repository.  
Every pull request must follow this guide.

## Setup instructions

## Build dependencies

1. Go 1.25 or newer
2. CMake 3.28 or newer
3. C++ toolchain (MSVC 2022, GCC 14+, or Clang 17+)
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
