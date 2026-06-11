# Zeri Plugin SDK

## 1. Quick start (5 minutes)

This quick start builds the reference native plugin and loads it into Zeri.

```bash
git clone https://github.com/ilmartotch/ReplZeriEmgine.git
cd ReplZeriEmgine/examples/plugin-hello
cmake -S . -B build
cmake --build build --config Release
```

Copy the built shared library and manifest into your plugin directory:

- Linux/macOS: `~/.zeri/plugins/`
- Windows: `%USERPROFILE%\.zeri\plugins\`

```bash
# Linux example
mkdir -p ~/.zeri/plugins
cp build/plugin-hello.so ~/.zeri/plugins/
cp plugin.json ~/.zeri/plugins/
```

Start Zeri and verify:

1. `/plugins`
2. `$hello`
3. `world`

Expected output in hello context: `Hello, world!`

## 2. C ABI reference

Header: `src/Engines/Include/ZeriPluginABI.h`

```cpp
#pragma once
#define ZERI_PLUGIN_ABI_VERSION 1

extern "C" {
    typedef const char* (*zeri_plugin_name_fn)();
    typedef const char* (*zeri_plugin_version_fn)();
    typedef int (*zeri_plugin_abi_version_fn)();
    typedef Zeri::Engines::IExecutor* (*zeri_create_executor_fn)();
    typedef Zeri::Engines::IContext* (*zeri_create_context_fn)(Zeri::Core::RuntimeState&);
    typedef void (*zeri_destroy_executor_fn)(Zeri::Engines::IExecutor*);
    typedef void (*zeri_destroy_context_fn)(Zeri::Engines::IContext*);
}
```

### Required exports

1. `zeri_plugin_name`
2. `zeri_plugin_version`
3. `zeri_plugin_abi_version` and it must return `ZERI_PLUGIN_ABI_VERSION`

### Optional exports

1. `zeri_create_executor` and `zeri_destroy_executor`
2. `zeri_create_context` and `zeri_destroy_context`

### Lifecycle contract

1. Zeri loads the dynamic library (`LoadLibrary` on Windows, `dlopen` on Linux/macOS).
2. Zeri validates ABI version before using plugin symbols.
3. Zeri probes factory functions in protected calls; failing plugins are skipped.
4. On shutdown Zeri unloads plugin libraries.

### ABI versioning guarantee

ABI v1 is frozen. Do not change existing exported signatures. Add new capabilities in a future ABI version only.

## 3. Lua plugin API reference

Lua plugins are plain `.lua` files in `~/.zeri/plugins/` and must return a table:

```lua
return {
  name = "my-plugin",
  version = "1.0.0",
  description = "Sample Lua plugin",
  commands = {
    ["greet"] = function(args)
      return "Hello, " .. (args[1] or "world") .. "!"
    end
  }
}
```

### Sandbox

Each plugin file runs in an isolated `lua_State`. Available APIs:

1. `string`, `table`, `math`
2. `io.read`
3. `zeri.write(text)`
4. `zeri.get(key)`
5. `zeri.set(key, value)`

Blocked globals:

1. `os`
2. `load`
3. `dofile`
4. `io.write` is not exposed

Every load and command invocation is wrapped with `lua_pcall`.

## 4. `plugin.json` manifest

Schema file: `docs/plugin-manifest-schema.json`

```json
{
  "name": "my-plugin",
  "version": "1.0.0",
  "description": "Example plugin",
  "author": "Example Author",
  "abi_version": 1,
  "entry": "my-plugin.dll"
}
```

Field meanings:

1. `name`: plugin identifier.
2. `version`: semantic version string.
3. `description`: short one-line description.
4. `author`: plugin author.
5. `abi_version`: must match `ZERI_PLUGIN_ABI_VERSION`.
6. `entry`: shared library file name.

## 5. Build instructions

### Windows (MSVC)

```cmake
add_library(my-plugin SHARED plugin.cpp)
set_target_properties(my-plugin PROPERTIES PREFIX "" OUTPUT_NAME "my-plugin")
target_compile_features(my-plugin PRIVATE cxx_std_23)
target_compile_options(my-plugin PRIVATE /W4 /WX /permissive-)
```

Output extension: `.dll`

### Linux

```cmake
add_library(my-plugin SHARED plugin.cpp)
set_target_properties(my-plugin PROPERTIES PREFIX "" OUTPUT_NAME "my-plugin")
target_compile_features(my-plugin PRIVATE cxx_std_23)
target_compile_options(my-plugin PRIVATE -Wall -Wextra -Werror -pedantic)
```

Output extension: `.so`

### macOS

```cmake
add_library(my-plugin SHARED plugin.cpp)
set_target_properties(my-plugin PROPERTIES PREFIX "" OUTPUT_NAME "my-plugin")
target_compile_features(my-plugin PRIVATE cxx_std_23)
target_compile_options(my-plugin PRIVATE -Wall -Wextra -Werror -pedantic)
```

Output extension: `.dylib`

## 6. Registry submission

Plugin registry repository: https://github.com/ilmartotch/zeri-plugins

Submission flow:

1. Fork `ilmartotch/zeri-plugins`.
2. Add your plugin entry in `registry.json`.
3. Open a pull request with plugin name, version, repository URL, and compatibility notes.
4. Wait for review and merge.
