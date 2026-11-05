cmake_minimum_required(VERSION 3.20)
project(MegaRacePort LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

# ---- SDL2 (static) ---------------------------------------------------------
set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON  CACHE BOOL "" FORCE)
set(SDL_TEST   OFF CACHE BOOL "" FORCE)
FetchContent_Declare(SDL2
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG        release-2.30.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(SDL2)

# ---- Lua 5.4 (built from source; upstream has no CMake) ---------------------
FetchContent_Declare(lua
    GIT_REPOSITORY https://github.com/lua/lua.git
    GIT_TAG        v5.4.7
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(lua)

file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(lua_static STATIC ${LUA_SOURCES})
target_include_directories(lua_static PUBLIC ${lua_SOURCE_DIR})
set_target_properties(lua_static PROPERTIES LINKER_LANGUAGE C)
if(UNIX AND NOT APPLE)
    target_compile_definitions(lua_static PRIVATE LUA_USE_LINUX)
    target_link_libraries(lua_static PUBLIC m dl)
endif()

# ---- Engine ----------------------------------------------------------------
add_executable(megarace
    src/main.cpp
    src/vfs.cpp
    src/video.cpp
    src/audio.cpp
    src/input.cpp
    src/lua_api.cpp
    src/formats/binreader.cpp
    src/formats/cryo.cpp)

target_include_directories(megarace PRIVATE src)
target_link_libraries(megarace PRIVATE SDL2::SDL2-static SDL2::SDL2main lua_static)

if(MSVC)
    target_compile_options(megarace PRIVATE /W4 /permissive-)
    set_property(TARGET megarace PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_compile_options(megarace PRIVATE -Wall -Wextra)
endif()

# Copy Lua scripts next to the binary so it runs out of the build folder.
add_custom_command(TARGET megarace POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:megarace>/scripts)
