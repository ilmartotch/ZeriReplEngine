cmake_minimum_required(VERSION 3.28)

if (DEFINED ENV{VCPKG_ROOT})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake" CACHE STRING "")
endif()

project(ZeriEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SOURCES
    src/main.cpp
    src/Core/Source/RuntimeState.cpp
    src/Engines/Source/SimpleParser.cpp
    src/Engines/Source/StandardDispatcher.cpp
    src/Engines/Source/BuiltinExecutor.cpp
    src/Engines/Source/LuaExecutorStub.cpp
    src/Extensions/Source/ExtensionManager.cpp
    src/Ui/Source/TerminalUi.cpp
)

add_executable(ZeriEngine ${SOURCES})

target_include_directories(ZeriEngine PRIVATE
    src
)