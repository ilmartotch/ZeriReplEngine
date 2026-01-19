cmake_minimum_required(VERSION 3.28)
set(CMAKE_TOOLCHAIN_FILE "${CMAKE_SOURCE_DIR}/vcpkg/scripts/buildsystems/vcpkg.cmake")

project(ZeriEngine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(ZeriEngine
    src/main.cpp

    # Engine
    "src/Engine/Source/ReplEngine.cpp" "src/Engine/Include/ReplEngine.h" 
    
    # Ui Interface
    "src/Interface/Include/TerminalUi.h" "src/Interface/Source/TerminalUi.cpp"
 "src/Engine/Include/ExecutionResult.h" "src/Engine/Include/InputProcessor.h" "src/Engine/Source/InputProcessor.cpp")