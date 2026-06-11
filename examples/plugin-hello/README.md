# plugin-hello

Reference native plugin for the Zeri Plugin SDK.

## Build

```bash
cmake -S . -B build
cmake --build build --config Release
```

The generated shared library name is:

1. `plugin-hello.dll` on Windows
2. `plugin-hello.so` on Linux
3. `plugin-hello.dylib` on macOS

## Install

Copy the generated library and `plugin.json` into:

- `~/.zeri/plugins/`

If your platform is not Windows, update `entry` in `plugin.json` to `.so` or `.dylib`.

## Expected behavior

1. Start `zeri`
2. Run `/plugins` and verify `hello-context v1.0.0`
3. Switch context with `$hello`
4. Type `world`
5. Output: `Hello, world!`
