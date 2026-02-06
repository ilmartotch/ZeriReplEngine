cmake_minimum_required(VERSION 3.28)

if (DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(ZeriEngine 
    VERSION 0.3.0
    DESCRIPTION "Modular REPL Orchestrator"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Threads REQUIRED)

set(CORE_SOURCES
    src/Core/Source/RuntimeState.cpp
    src/Core/Source/SystemGuard.cpp # Added
)

set(MODULE_SOURCES
    src/Modules/Source/ModuleManager.cpp
)

set(ENGINE_SOURCES
    src/Engines/Source/MetaParser.cpp
    
    src/Engines/Source/BuiltinExecutor.cpp
    src/Engines/Source/LuaExecutorStub.cpp
    src/Engines/Source/GlobalContext.cpp
    src/Engines/Source/MathContext.cpp
    src/Engines/Source/ProcessBridge.cpp
    src/Engines/Source/SandboxContext.cpp
    src/Engines/Source/SetupContext.cpp # Added
)

set(EXTENSION_SOURCES
    src/Extensions/Source/ExtensionManager.cpp
)

set(UI_SOURCES
    src/Ui/Source/TerminalUi.cpp
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
    ${UI_SOURCES}
)

target_include_directories(ZeriEngine PRIVATE
    src
    src/Core/Include
    src/Modules/Include
    src/Engines/Include
    src/Extensions/Include
    src/Ui/Include
)

target_link_libraries(ZeriEngine PRIVATE 
    Threads::Threads
)

if(MSVC)
    target_compile_options(ZeriEngine PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(ZeriEngine PRIVATE -Wall -Wextra -Werror -pedantic)
endif()

message(STATUS "Project Zeri v${PROJECT_VERSION} configured successfully.")
