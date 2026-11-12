# SDL2 for the Xbox 360 (and for its stand-in on a PC, MEGARACE_XENON_SIM):
# SDL's own platform-neutral code with the software renderer, built with
# platform/xenon/sdl/SDL_config.h and the drivers next to it.
#   in:  SDL2_SRC (SDL's source folder)
#   out: SDL2::SDL2-static, SDL2::SDL2main (empty)

set(xsdl_dir ${CMAKE_CURRENT_LIST_DIR}/sdl)
set(xsdl_inc ${CMAKE_BINARY_DIR}/xenon-sdl-include)

# SDL's public headers, with our SDL_config.h in place of theirs.
file(GLOB xsdl_headers ${SDL2_SRC}/include/*.h)
file(COPY ${xsdl_headers} DESTINATION ${xsdl_inc})
file(COPY ${xsdl_dir}/SDL_config.h DESTINATION ${xsdl_inc})

set(xsdl_src_dirs
    atomic audio cpuinfo dynapi events file libm stdlib render render/software
    video video/yuv2rgb thread thread/generic locale locale/dummy misc misc/dummy
    sensor sensor/dummy power joystick haptic)
file(GLOB xsdl_sources ${SDL2_SRC}/src/*.c)
foreach(d ${xsdl_src_dirs})
    file(GLOB s ${SDL2_SRC}/src/${d}/*.c)
    list(APPEND xsdl_sources ${s})
endforeach()
list(APPEND xsdl_sources ${SDL2_SRC}/src/timer/SDL_timer.c)
# Not for this platform (the dynamic API is off; no device files).
list(FILTER xsdl_sources EXCLUDE REGEX "/SDL_audiodev\\.c$|/SDL_dynapi\\.c$|/SDL_gamecontroller\\.c$|/controller_type\\.c$|/SDL_steam_virtual_gamepad\\.c$")
list(APPEND xsdl_sources
    ${xsdl_dir}/SDL_xenon_video.c
    ${xsdl_dir}/SDL_xenon_audio.c
    ${xsdl_dir}/SDL_xenon_timer.c
    ${xsdl_dir}/SDL_xenon_filesystem.c)
if(MEGARACE_XENON_SIM)
    list(APPEND xsdl_sources ${CMAKE_CURRENT_LIST_DIR}/xenon_platform_sim.c)
else()
    list(APPEND xsdl_sources ${CMAKE_CURRENT_LIST_DIR}/xenon_platform.c)
endif()

add_library(sdl2_xenon STATIC ${xsdl_sources})
target_include_directories(sdl2_xenon PUBLIC ${xsdl_inc} ${CMAKE_CURRENT_LIST_DIR})
target_include_directories(sdl2_xenon PRIVATE ${SDL2_SRC}/src)
# Turns SDL's dynamic API off (src/dynapi/SDL_dynapi.h can only be told so
# this way); on GCC it changes nothing else.
target_compile_definitions(sdl2_xenon PRIVATE SDL_THREAD_SAFETY_ANALYSIS=1)
if(NOT MSVC)
    target_compile_options(sdl2_xenon PRIVATE -w)
endif()
add_library(SDL2::SDL2-static ALIAS sdl2_xenon)
add_library(sdl2_xenon_main INTERFACE)
add_library(SDL2::SDL2main ALIAS sdl2_xenon_main)
