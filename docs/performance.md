# Startup performance

This document tracks startup latency for the full chain:

1. Go binary entry
2. preflight/bootstrap
3. engine spawn
4. IPC connect and handshake
5. first prompt render

## Profiling command

```bash
zeri --no-onboarding --profile-startup
```

The command prints four timing lines to `stderr` and exits after the first prompt render.

## Before optimization baseline

Run 5 times per platform and record the median.

| Platform | Median total (ms) | Notes |
| --- | ---: | --- |
| macOS arm64 (M1) | TBD | Collect with release binary |
| Linux x64 | TBD | Collect with release binary |

## Changes implemented in F10

- Lazy language-context executor initialization moved from context constructors to first `OnEnter()`.
- `ModuleManager` background scan deferred until first user command and started after 500ms.
- Plugin and extension discovery deferred until first user command.
- Startup profiling instrumentation added in UI startup path:
  - `binary entry → preflight complete`
  - `engine spawn → IPC socket available`
  - `IPC connect → handshake validated`
  - `handshake → first prompt rendered`

## After optimization measurements

Run 5 times per platform and record the median.

| Platform | Median total (ms) | Target |
| --- | ---: | ---: |
| macOS arm64 (M1) | TBD | < 200 |
| Linux x64 | TBD | Track |

## Reproducibility notes

- Use `--no-onboarding` for stable startup measurement.
- Use release binaries.
- Run with warm disk cache and then with cold cache if needed.
- Capture all four segment lines for each run to identify regressions.

