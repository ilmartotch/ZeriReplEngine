cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VCPKG_ROOT})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    elseif(EXISTS "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")
        set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
    else()
        message(FATAL_ERROR "VCPKG_ROOT is not set and no local vcpkg toolchain was found. Set VCPKG_ROOT or clone vcpkg into ${CMAKE_SOURCE_DIR}/vcpkg.")
    endif()
endif()

project(ZeriEngine 
    VERSION 1.0.0
    DESCRIPTION "Modular REPL Orchestrator"
    LANGUAGES CXX
)

include(GNUInstallDirs)
include(CTest)

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_CURRENT_BINARY_DIR}/generated/version.h"
    @ONLY
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# NOMINMAX prevents Windows.h from defining min/max macros that conflict with <algorithm>.
# _WIN32_WINNT=0x0A00 targets Windows 10, enabling AF_UNIX socket support required by
# asio::local::stream_protocol for the Yuumi IPC bridge.
add_compile_definitions(NOMINMAX)
if(WIN32)
    add_compile_definitions(_WIN32_WINNT=0x0A00)
endif()

find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_path(EXPRTK_INCLUDE_DIRS "exprtk.hpp")
if(NOT EXPRTK_INCLUDE_DIRS)
    message(FATAL_ERROR "exprtk not found. Install via vcpkg: vcpkg install exprtk")
endif()

find_package(asio CONFIG REQUIRED)

add_library(yuumi INTERFACE)
add_library(yuumi::yuumi ALIAS yuumi)
target_compile_features(yuumi INTERFACE cxx_std_23)
target_include_directories(yuumi INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(yuumi INTERFACE
    Threads::Threads
    asio::asio
    nlohmann_json::nlohmann_json
)
if(WIN32)
    target_compile_definitions(yuumi INTERFACE NOMINMAX)
endif()

set(CORE_SOURCES
    src/Core/Source/BugSnapshot.cpp
    src/Core/Source/AppPaths.cpp
    src/Core/Source/ContextManager.cpp
    src/Core/Source/HelpCatalog.cpp
    src/Core/Source/RuntimeState.cpp
    src/Core/Source/StartupDiagnostics.cpp
    src/Core/Source/SystemGuard.cpp
    src/Core/Source/UserPaths.cpp
    src/Core/Source/VariableScope.cpp
)

set(MODULE_SOURCES
    src/Modules/Source/ModuleManager.cpp
)

set(ENGINE_SOURCES
    src/Engines/Source/MetaParser.cpp
    src/Engines/Source/BuiltinExecutor.cpp
    src/Engines/Source/ExpressionExecutor.cpp
    src/Engines/Source/LuaExecutor.cpp
    src/Engines/Source/LuaContext.cpp
    src/Engines/Source/JsExecutor.cpp
    src/Engines/Source/JsContext.cpp
    src/Engines/Source/PythonExecutor.cpp
    src/Engines/Source/PythonContext.cpp
    src/Engines/Source/RubyExecutor.cpp
    src/Engines/Source/RubyContext.cpp
    src/Engines/Source/GlobalContext.cpp
    src/Engines/Source/MathContext.cpp
    src/Engines/Source/ProcessBridge.cpp
    src/Engines/Source/SandboxContext.cpp
    src/Engines/Source/DefaultDispatcher.cpp
    src/Engines/Source/CachedDispatcher.cpp
    src/Engines/Source/CompletionEngine.cpp
    src/Engines/Source/LuaPluginLoader.cpp
    src/Engines/Source/PluginLoader.cpp
    src/Engines/Source/ScriptEditorContext.cpp
    src/Engines/Source/ScriptHubContext.cpp
    src/Engines/Source/CustomCommandContext.cpp
)

set(ZERILINK_SOURCES
    src/ZeriLink/Source/ZeriWireProtocol.cpp
    src/ZeriLink/Source/ProcessBridge.cpp
)

set(BRIDGE_SOURCES
    engine/src/bridge/ZeriWireSidecarBridge.cpp
)

if(WIN32)
    list(APPEND ZERILINK_SOURCES src/ZeriLink/Source/WindowsProcessHost.cpp)
else()
    list(APPEND ZERILINK_SOURCES src/ZeriLink/Source/PosixProcessHost.cpp)
endif()

set(UI_SOURCES
    src/Ui/Source/BridgeTerminal.cpp
    src/Ui/Source/OutputSink.cpp
)

set(MAIN_SOURCE
    src/main.cpp
)

add_executable(ZeriEngine 
    ${MAIN_SOURCE}
    ${CORE_SOURCES}
    ${MODULE_SOURCES}
    ${ENGINE_SOURCES}
    ${ZERILINK_SOURCES}
    ${BRIDGE_SOURCES}
    ${UI_SOURCES}
)

set_target_properties(ZeriEngine PROPERTIES OUTPUT_NAME "zeri-engine")

target_include_directories(ZeriEngine PRIVATE
    src
    src/Core/Include
    src/Modules/Include
    src/Engines/Include
    src/ZeriLink/Include
    src/Ui/Include
    "${CMAKE_CURRENT_BINARY_DIR}/generated"
    ${EXPRTK_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(ZeriEngine PRIVATE 
    Threads::Threads
    nlohmann_json::nlohmann_json
    ${LUA_LIBRARIES}
    yuumi::yuumi
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ZeriEngine PRIVATE dl)
endif()
target_compile_definitions(ZeriEngine PRIVATE ZERI_ENGINE_VERSION="${CMAKE_PROJECT_VERSION}")

if(MSVC)
    target_compile_options(ZeriEngine PRIVATE /W4 /WX /permissive- /bigobj /wd4702)
else()
    target_compile_options(ZeriEngine PRIVATE -Wall -Wextra -Werror -pedantic -Wno-ignored-attributes
            -Wno-missing-field-initializers
        )
endif()

# Link Psapi on Windows for GetProcessMemoryInfo (RAM monitor)
if(WIN32)
    target_link_libraries(ZeriEngine PRIVATE psapi)
endif()

# Copy ZeriLink runtime sidecars to the output directory
add_custom_command(TARGET ZeriEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/runtime"
        "$<TARGET_FILE_DIR:ZeriEngine>/Runtime"
    COMMENT "Copying ZeriLink runtimes to output directory"
)

add_custom_command(TARGET ZeriEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/runtime"
        "$<TARGET_FILE_DIR:ZeriEngine>/runtime"
    COMMENT "Copying runtime assets to output directory"
)

add_custom_command(TARGET ZeriEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/help"
        "$<TARGET_FILE_DIR:ZeriEngine>/help"
    COMMENT "Copying help catalog to output directory"
)

install(TARGETS ZeriEngine
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/runtime"
    DESTINATION .
)

install(DIRECTORY "${CMAKE_SOURCE_DIR}/help"
    DESTINATION .
)

if(BUILD_TESTING)
    file(WRITE "${CMAKE_BINARY_DIR}/_invalid_help_catalog.json" "{ invalid json }\n")
    file(WRITE "${CMAKE_BINARY_DIR}/_invalid_schema_help_catalog.json" "{ \"contexts\": [] }\n")

    add_executable(zeri_help_catalog_smoke
        src/Tests/HelpCatalogSmoke.cpp
        src/Core/Source/AppPaths.cpp
        src/Core/Source/HelpCatalog.cpp
    )

    target_include_directories(zeri_help_catalog_smoke PRIVATE
        src
        src/Core/Include
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )

    target_link_libraries(zeri_help_catalog_smoke PRIVATE
        nlohmann_json::nlohmann_json
    )

    if(MSVC)
        target_compile_options(zeri_help_catalog_smoke PRIVATE /W4 /WX /permissive- /bigobj /wd4702)
    else()
        target_compile_options(zeri_help_catalog_smoke PRIVATE -Wall -Wextra -Werror -pedantic -Wno-ignored-attributes
                -Wno-missing-field-initializers
            )
    endif()

    add_test(
        NAME helpcatalog-missing-safe
        COMMAND zeri_help_catalog_smoke --expect-loaded=false --expect-error-contains=not found
    )
    set_tests_properties(helpcatalog-missing-safe PROPERTIES
        ENVIRONMENT "ZERI_HELP_CATALOG_PATH=${CMAKE_BINARY_DIR}/_missing_help_catalog.json"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_test(
        NAME helpcatalog-loaded-explicit
        COMMAND zeri_help_catalog_smoke --expect-loaded=true
    )
    set_tests_properties(helpcatalog-loaded-explicit PROPERTIES
        ENVIRONMENT "ZERI_HELP_CATALOG_PATH=${CMAKE_SOURCE_DIR}/help/help_catalog.json"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_test(
        NAME helpcatalog-invalid-json-explicit
        COMMAND zeri_help_catalog_smoke --expect-loaded=false --expect-error-contains=parse
    )
    set_tests_properties(helpcatalog-invalid-json-explicit PROPERTIES
        ENVIRONMENT "ZERI_HELP_CATALOG_PATH=${CMAKE_BINARY_DIR}/_invalid_help_catalog.json"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_test(
        NAME helpcatalog-invalid-schema-explicit
        COMMAND zeri_help_catalog_smoke --expect-loaded=false --expect-error-contains=schema
    )
    set_tests_properties(helpcatalog-invalid-schema-explicit PROPERTIES
        ENVIRONMENT "ZERI_HELP_CATALOG_PATH=${CMAKE_BINARY_DIR}/_invalid_schema_help_catalog.json"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_test(
        NAME helpcatalog-random-cwd-no-crash
        COMMAND zeri_help_catalog_smoke
    )
    set_tests_properties(helpcatalog-random-cwd-no-crash PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_executable(zeri_unit_meta_parser
        test/unit/test_meta_parser.cpp
        src/Engines/Source/MetaParser.cpp
    )
    target_include_directories(zeri_unit_meta_parser PRIVATE
        src
        src/Core/Include
        src/Engines/Include
        src/Engines/Include/Interface
    )
    if(MSVC)
        target_compile_options(zeri_unit_meta_parser PRIVATE /W4 /WX /permissive- /bigobj /wd4702)
    else()
        target_compile_options(zeri_unit_meta_parser PRIVATE -Wall -Wextra -Werror -pedantic -Wno-ignored-attributes
                -Wno-missing-field-initializers
            )
    endif()
    add_test(
        NAME MetaParser-suite
        COMMAND zeri_unit_meta_parser
    )
    set_tests_properties(MetaParser-suite PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_executable(zeri_unit_variable_scope
        test/unit/test_variable_scope.cpp
        src/Core/Source/VariableScope.cpp
    )
    target_include_directories(zeri_unit_variable_scope PRIVATE
        src
        src/Core/Include
    )
    if(MSVC)
        target_compile_options(zeri_unit_variable_scope PRIVATE /W4 /WX /permissive- /bigobj /wd4702)
    else()
        target_compile_options(zeri_unit_variable_scope PRIVATE -Wall -Wextra -Werror -pedantic -Wno-ignored-attributes
                -Wno-missing-field-initializers
            )
    endif()
    add_test(
        NAME VariableScope-suite
        COMMAND zeri_unit_variable_scope
    )
    set_tests_properties(VariableScope-suite PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_executable(zeri_unit_runtime_state_shared
        test/unit/test_runtime_state_shared.cpp
        src/Core/Source/RuntimeState.cpp
        src/Core/Source/ContextManager.cpp
        src/Core/Source/UserPaths.cpp
        src/Core/Source/AppPaths.cpp
        src/Modules/Source/ModuleManager.cpp
    )
    target_include_directories(zeri_unit_runtime_state_shared PRIVATE
        src
        src/Core/Include
        src/Engines/Include
        src/Engines/Include/Interface
        src/Modules/Include
        src/Ui/Include
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(zeri_unit_runtime_state_shared PRIVATE
        Threads::Threads
        nlohmann_json::nlohmann_json
    )
    if(MSVC)
        target_compile_options(zeri_unit_runtime_state_shared PRIVATE /W4 /WX /permissive- /bigobj /wd4702)
    else()
        target_compile_options(zeri_unit_runtime_state_shared PRIVATE -Wall -Wextra -Werror -pedantic -Wno-ignored-attributes
                -Wno-missing-field-initializers
            )
    endif()
    add_test(
        NAME RuntimeStateShared-suite
        COMMAND zeri_unit_runtime_state_shared
    )
    set_tests_properties(RuntimeStateShared-suite PROPERTIES
        ENVIRONMENT "APPDATA=${CMAKE_BINARY_DIR}/test-isolation/runtime-state-shared;USERPROFILE=${CMAKE_BINARY_DIR}/test-isolation/runtime-state-shared;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-isolation/runtime-state-shared;HOME=${CMAKE_BINARY_DIR}/test-isolation/runtime-state-shared"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_executable(zeri_unit_context_manager
        test/unit/test_context_manager.cpp
        src/Core/Source/ContextManager.cpp
    )
    target_include_directories(zeri_unit_context_manager PRIVATE
        src
        src/Core/Include
        src/Engines/Include
        src/Engines/Include/Interface
        src/Ui/Include
    )
    if(MSVC)
        target_compile_options(zeri_unit_context_manager PRIVATE /W4 /WX /permissive- /bigobj /wd4702)
    else()
        target_compile_options(zeri_unit_context_manager PRIVATE -Wall -Wextra -Werror -pedantic -Wno-ignored-attributes
                -Wno-missing-field-initializers
            )
    endif()
    add_test(
        NAME ContextManager-suite
        COMMAND zeri_unit_context_manager
    )
    set_tests_properties(ContextManager-suite PROPERTIES
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )

    add_executable(zeri_unit_script_registry
        test/unit/test_script_registry.cpp
        src/Core/Source/RuntimeState.cpp
        src/Core/Source/ContextManager.cpp
        src/Core/Source/UserPaths.cpp
        src/Core/Source/AppPaths.cpp
        src/Modules/Source/ModuleManager.cpp
    )
    target_include_directories(zeri_unit_script_registry PRIVATE
        src
        src/Core/Include
        src/Engines/Include
        src/Engines/Include/Interface
        src/Modules/Include
        src/Ui/Include
        "${CMAKE_CURRENT_BINARY_DIR}/generated"
    )
    target_link_libraries(zeri_unit_script_registry PRIVATE
        Threads::Threads
        nlohmann_json::nlohmann_json
    )
    if(MSVC)
        target_compile_options(zeri_unit_script_registry PRIVATE /W4 /WX /permissive- /bigobj /wd4702)
    else()
        target_compile_options(zeri_unit_script_registry PRIVATE -Wall -Wextra -Werror -pedantic -Wno-ignored-attributes
                -Wno-missing-field-initializers
            )
    endif()
    add_test(
        NAME ScriptRegistry-suite
        COMMAND zeri_unit_script_registry
    )
    set_tests_properties(ScriptRegistry-suite PROPERTIES
        ENVIRONMENT "APPDATA=${CMAKE_BINARY_DIR}/test-isolation/script-registry;USERPROFILE=${CMAKE_BINARY_DIR}/test-isolation/script-registry;XDG_CONFIG_HOME=${CMAKE_BINARY_DIR}/test-isolation/script-registry;HOME=${CMAKE_BINARY_DIR}/test-isolation/script-registry"
        WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
    )
endif()

message(STATUS "Project Zeri v${PROJECT_VERSION} configured successfully.")
