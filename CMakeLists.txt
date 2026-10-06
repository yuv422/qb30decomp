cmake_minimum_required(VERSION 3.16)
project(qb30decomp LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(ZYDIS_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_DOXYGEN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_MAN OFF CACHE BOOL "" FORCE)
set(ZYDIS_BUILD_TESTS OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    zydis
    GIT_REPOSITORY https://github.com/zyantific/zydis.git
    GIT_TAG        v4.1.1
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(zydis)

add_executable(qb30decomp
    src/main.cpp
    src/exe_loader.cpp
    src/mbf.cpp
    src/opcodes.cpp
    src/disasm.cpp
    src/ir.cpp
    src/cfg.cpp
    src/simplify.cpp
    src/lifter.cpp
    src/lifter_x86.cpp
    src/lifter_rt.cpp
    src/printer.cpp
    src/decompiler.cpp
    src/emitter.cpp
    src/postprocess.cpp
)
target_link_libraries(qb30decomp PRIVATE Zydis)
if(MSVC)
    target_compile_options(qb30decomp PRIVATE /W4 /permissive- /utf-8 /wd4100)
    target_compile_definitions(qb30decomp PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(qb30decomp PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()
