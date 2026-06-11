cmake_minimum_required(VERSION 3.28)

project(zeri_plugin_hello LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(plugin-hello SHARED plugin_hello.cpp)

set_target_properties(plugin-hello PROPERTIES
    PREFIX ""
    OUTPUT_NAME "plugin-hello"
)

target_include_directories(plugin-hello PRIVATE
    ../../src
    ../../src/Core/Include
    ../../src/Engines/Include
    ../../src/Engines/Include/Interface
    ../../src/Ui/Include
)

if(MSVC)
    target_compile_options(plugin-hello PRIVATE /W4 /WX /permissive-)
else()
    target_compile_options(plugin-hello PRIVATE -Wall -Wextra -Werror -pedantic)
endif()
