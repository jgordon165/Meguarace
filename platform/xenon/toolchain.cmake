# CMake toolchain for the Xbox 360 with LibXenon (Free60): the xenon-gcc
# cross compiler from the free60/libxenon Docker image (DEVKITXENON).
#   cmake -B build-xenon -DCMAKE_TOOLCHAIN_FILE=platform/xenon/toolchain.cmake
# The flags are LibXenon's own (devkitxenon/rules). Programs come out as
# .elf, and as .elf32 for XeLL.

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR powerpc64)
set(MEGARACE_XENON_TARGET ON CACHE BOOL "Building for the Xbox 360" FORCE)

if(DEFINED ENV{DEVKITXENON})
    set(DEVKITXENON $ENV{DEVKITXENON})
else()
    set(DEVKITXENON /usr/local/xenon)
endif()

set(CMAKE_C_COMPILER   ${DEVKITXENON}/bin/xenon-gcc)
set(CMAKE_CXX_COMPILER ${DEVKITXENON}/bin/xenon-g++)
set(CMAKE_AR           ${DEVKITXENON}/bin/xenon-ar CACHE FILEPATH "")
set(CMAKE_RANLIB       ${DEVKITXENON}/bin/xenon-ranlib CACHE FILEPATH "")
set(XENON_OBJCOPY      ${DEVKITXENON}/bin/xenon-objcopy)

set(XENON_MACHDEP "-DXENON -m32 -maltivec -fno-pic -mpowerpc64 -mhard-float")
set(CMAKE_C_FLAGS_INIT   "${XENON_MACHDEP} -I${DEVKITXENON}/usr/include")
set(CMAKE_CXX_FLAGS_INIT "${XENON_MACHDEP} -I${DEVKITXENON}/usr/include")
set(CMAKE_EXE_LINKER_FLAGS_INIT
    "${XENON_MACHDEP} -L${DEVKITXENON}/xenon/lib/32 -L${DEVKITXENON}/usr/lib -n -T ${DEVKITXENON}/app.lds -Wl,--gc-sections")

set(CMAKE_FIND_ROOT_PATH ${DEVKITXENON} ${DEVKITXENON}/usr)
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
