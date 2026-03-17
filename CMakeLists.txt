cmake_minimum_required(VERSION 3.28)

if (DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(ZeriEngine 
    VERSION 1.0.0
    DESCRIPTION "Modular REPL Orchestrator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define a macro for the project version for use min and max functions in Windows headers
add_compile_definitions(NOMINMAX)

find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(replxx CONFIG REQUIRED)
find_package(Lua REQUIRED)
find_package(ftxui CONFIG REQUIRED)
find_path(EXPRTK_INCLUDE_DIRS "exprtk.hpp")

set(CORE_SOURCES
    src/Core/Source/ContextManager.cpp
    src/Core/Source/RuntimeState.cpp
    src/Core/Source/SystemGuard.cpp
    src/Core/Source/VariableScope.cpp
)

set(MODULE_SOURCES
    src/Modules/Source/ModuleManager.cpp
)

set(ENGINE_SOURCES
    src/Engines/Source/MetaParser.cpp
    src/Engines/Source/MetaLanguageParser.cpp
    src/Engines/Source/BuiltinExecutor.cpp
    src/Engines/Source/ContextCommandExecutor.cpp
    src/Engines/Source/ExpressionExecutor.cpp
    src/Engines/Source/LuaExecutor.cpp
    src/Engines/Source/GlobalContext.cpp
    src/Engines/Source/MathContext.cpp
    src/Engines/Source/ProcessBridge.cpp
    src/Engines/Source/SandboxContext.cpp
    src/Engines/Source/SetupContext.cpp
    src/Engines/Source/DefaultDispatcher.cpp
    src/Engines/Source/CachedDispatcher.cpp
)

set(EXTENSION_SOURCES
    src/Extensions/Source/ExtensionManager.cpp
)

set(ZERILINK_SOURCES
    src/ZeriLink/Source/ZeriWireProtocol.cpp
    src/ZeriLink/Source/ProcessBridge.cpp
)

if(WIN32)
    list(APPEND ZERILINK_SOURCES src/ZeriLink/Source/WindowsProcessHost.cpp)
else()
    list(APPEND ZERILINK_SOURCES src/ZeriLink/Source/PosixProcessHost.cpp)
endif()

set(UI_SOURCES
    src/Ui/Source/TerminalUi.cpp
    src/Ui/Source/FtxUiTerminal.cpp
)

set(MAIN_SOURCE
    src/main.cpp
)

add_executable(ZeriEngine 
    ${MAIN_SOURCE}
    ${CORE_SOURCES}
    ${MODULE_SOURCES}
    ${ENGINE_SOURCES}
    ${EXTENSION_SOURCES}
    ${ZERILINK_SOURCES}
    ${UI_SOURCES}
)

target_include_directories(ZeriEngine PRIVATE
    src
    src/Core/Include
    src/Modules/Include
    src/Engines/Include
    src/Extensions/Include
    src/ZeriLink/Include
    src/Ui/Include
    ${EXPRTK_INCLUDE_DIRS}
    ${LUA_INCLUDE_DIR}
)

target_link_libraries(ZeriEngine PRIVATE 
    Threads::Threads
    nlohmann_json::nlohmann_json
    replxx::replxx
    ${LUA_LIBRARIES}
    ftxui::screen
    ftxui::dom
    ftxui::component
)

if(MSVC)
    target_compile_options(ZeriEngine PRIVATE /W4 /WX /permissive- /bigobj)
else()
    target_compile_options(ZeriEngine PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

# Link Psapi on Windows for GetProcessMemoryInfo (RAM monitor)
if(WIN32)
    target_link_libraries(ZeriEngine PRIVATE psapi)
endif()

# Copy ZeriLink runtime sidecars to the output directory
add_custom_command(TARGET ZeriEngine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/src/ZeriLink/Runtime"
        "$<TARGET_FILE_DIR:ZeriEngine>/Runtime"
    COMMENT "Copying ZeriLink runtimes to output directory"
)

message(STATUS "Project Zeri v${PROJECT_VERSION} configured successfully.")
