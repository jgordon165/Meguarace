cmake_minimum_required(VERSION 3.20)
project(Meguarace VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(MEGARACE_TESTS "Build the test suite" ON)

# One static MSVC runtime for everything (SDL, Lua and the engine), so
# releases need no Visual C++ redistributable and nothing mismatches at link.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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
elseif(APPLE)
    target_compile_definitions(lua_static PRIVATE LUA_USE_MACOSX)
endif()

# ---- ymfm (Yamaha FM chip emulators, BSD-3; we use its OPL2) ---------------
# Pinned to a commit: the project has no release tags.
# SOURCE_SUBDIR points at a folder with no CMakeLists.txt, so only the
# sources are fetched (ymfm's own CMake would build every chip it has).
FetchContent_Declare(ymfm
    GIT_REPOSITORY https://github.com/aaronsgiles/ymfm.git
    GIT_TAG        81aec25ccbb98f4873a255f7551ac4dadac59b4a
    SOURCE_SUBDIR  sources-only)
FetchContent_MakeAvailable(ymfm)
add_library(ymfm_opl STATIC
    ${ymfm_SOURCE_DIR}/src/ymfm_opl.cpp
    ${ymfm_SOURCE_DIR}/src/ymfm_adpcm.cpp
    ${ymfm_SOURCE_DIR}/src/ymfm_pcm.cpp)
target_include_directories(ymfm_opl SYSTEM PUBLIC ${ymfm_SOURCE_DIR}/src)
target_compile_options(ymfm_opl PRIVATE $<IF:$<CXX_COMPILER_ID:MSVC>,/W0,-w>)   # third-party code

# ---- Warnings ----------------------------------------------------------------
function(megarace_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra)
    endif()
endfunction()

# ---- File formats (no SDL; shared by the engine and the tests) ---------------
add_library(megarace_formats STATIC
    src/vfs.cpp
    src/formats/binreader.cpp
    src/formats/cryo.cpp
    src/formats/herad.cpp
    src/fm.cpp)
target_include_directories(megarace_formats PUBLIC src)
target_link_libraries(megarace_formats PUBLIC ymfm_opl)
megarace_warnings(megarace_formats)

# ---- Engine ----------------------------------------------------------------
add_executable(megarace
    src/main.cpp
    src/video.cpp
    src/audio.cpp
    src/input.cpp
    src/lua_api.cpp)
if(WIN32)
    # The port's own icon on the .exe (res/meguarace.ico, drawn by
    # tools/make_icon.py; no game art).
    target_sources(megarace PRIVATE res/meguarace.rc)
endif()
target_link_libraries(megarace PRIVATE megarace_formats SDL2::SDL2-static SDL2::SDL2main lua_static)
megarace_warnings(megarace)
if(MSVC)
    # Debugging symbols for Release too (uploaded separately by the release workflow).
    target_compile_options(megarace PRIVATE $<$<CONFIG:Release>:/Zi>)
    target_link_options(megarace PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()

# Copy Lua scripts next to the binary on every build (not only when the C++
# changes), so script edits are picked up by a plain rebuild.
add_custom_target(copy_scripts ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_SOURCE_DIR}/scripts $<TARGET_FILE_DIR:megarace>/scripts
    COMMENT "Copying Lua scripts")
add_dependencies(copy_scripts megarace)

# ---- Tests -------------------------------------------------------------------
# No game data is in the repository, so CI runs only the data-free tests.
# Configure with MEGARACE_DATA=<your CD> in the environment to also run the
# tests against the real game files.
if(MEGARACE_TESTS)
    enable_testing()

    add_executable(megarace_tests tests/test_formats.cpp)
    target_link_libraries(megarace_tests PRIVATE megarace_formats)
    megarace_warnings(megarace_tests)
    add_test(NAME formats COMMAND megarace_tests)

    set(lua_test_args --scripts ${CMAKE_SOURCE_DIR}/tests/lua --frames 1)
    if(DEFINED ENV{MEGARACE_DATA})
        list(APPEND lua_test_args --data $ENV{MEGARACE_DATA})
        set(lua_test_env "MEGARACE_DATA=$ENV{MEGARACE_DATA}")
    endif()
    add_test(NAME lua COMMAND megarace ${lua_test_args})
    set_tests_properties(lua PROPERTIES ENVIRONMENT
        "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy;MEGARACE_SCRIPTS=${CMAKE_SOURCE_DIR}/scripts;${lua_test_env}")

    # The shipped game must at least boot to its "no data" screen.
    add_test(NAME boot COMMAND megarace --data ${CMAKE_BINARY_DIR}/no-such-folder --frames 3)
    set_tests_properties(boot PROPERTIES ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
endif()

# ---- Install / packaging -----------------------------------------------------
# Layout: megarace(.exe), scripts/, README.md, docs/. No game data, ever.
install(TARGETS megarace RUNTIME DESTINATION .)
install(DIRECTORY scripts DESTINATION .)
install(FILES README.md DESTINATION .)
install(FILES res/meguarace.png res/meguarace.ico DESTINATION .)   # the icon, for shortcuts and launchers
install(DIRECTORY docs DESTINATION .)
