cmake_minimum_required(VERSION 3.15)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_SOURCE_DIR}/cmake)

project(UE1 C CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_EXTENSIONS ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if (NOT EXISTS ${CMAKE_BINARY_DIR}/CMakeCache.txt)
  if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
  endif()
endif()

# Options

option(USE_SDL "Use NSDLDrv instead of WinDrv" ON)
option(BUILD_EDITOR "Build Editor (GUI support requires Windows)" ON)
option(BUILD_SOFTDRV "Build SoftDrv (x86/MSVC only)" OFF)
option(BUILD_NOPENGLDRV "Build NOpenGLDrv" OFF)
option(BUILD_NMINIGLDRV "Build NMiniGLDrv (AmigaOS only)" OFF)
set(AMIGA_SHARED_MINIGL_ROOT "/mnt/d/dev/Pistorm3D/Pistorm3D_v12" CACHE PATH
  "Path containing the dispatch-table minigl.library SDK")
option(BUILD_NOPENGLESDRV "Build NOpenGLESDrv" ON)
option(BUILD_NULLSOUNDDRV "Build SoundDrv (Null driver)" ON)
option(BUILD_NOPENALDRV "Build NOpenALDrv" ON)
option(BUILD_WINDRV "Build WinDrv" OFF)
option(BUILD_STATIC "Link everything into a single binary" OFF)
option(AMIGA_USE_OPENAL "Use the Amiga OpenAL/libmodplug audio backend" OFF)
option(AMIGA_MAX_OPT "Use aggressive 68k speed optimizations" OFF)
option(AMIGA_EXACT_LIGHT_INDEX "Use the PPC-equivalent single lightmap index on 68k" OFF)
option(AMIGA_PPC_COLOR_ALIASES "Use the portable PPC packed-light channel aliases on 68k" OFF)
option(AMIGA_PACKED_LIGHT_COORD "Advance the packed light coordinate with explicit carry on 68k" OFF)
option(AMIGA_PACKED_LIGHT_OS "Use the original four-tap packed light walk with explicit 32-bit carry on 68k" OFF)
option(AMIGA_MATCH_LIGHT_CHANNELS "Read packed light channels in the same BBGGRR order as textures" OFF)
option(LINUX_PPC "Build for 32-bit big-endian PowerPC Linux with SDL 1.2" OFF)
option(SOFTDRV_LIGHT_TRACE "Write a bounded software-lighting comparison trace" OFF)

if(SOFTDRV_LIGHT_TRACE)
  add_definitions(-DSOFTDRV_LIGHT_TRACE=1)
endif()

if(VITA)
  set(BUILD_STATIC ON)
endif()

if(AMIGA)
  # AmigaOS 3.x has no dlopen-style module loading, so everything is linked
  # into one executable.  The combined build contains both the fixed-function
  # OpenGL renderer and SoftDrv; Unreal.ini selects which one is instantiated
  # on the next launch.
  set(BUILD_STATIC ON)
  set(BUILD_EDITOR OFF)
  set(BUILD_SOFTDRV ON)
  set(BUILD_WINDRV OFF)
  set(AMIGA_GL_BACKEND "MiniGL" CACHE STRING
    "Amiga GL backend: OpenGL (StormMesa) or MiniGL")
  set_property(CACHE AMIGA_GL_BACKEND PROPERTY STRINGS OpenGL MiniGL)
  if(AMIGA_GL_BACKEND STREQUAL "MiniGL")
    set(BUILD_NOPENGLDRV OFF)
    set(BUILD_NMINIGLDRV ON)
  else()
    set(BUILD_NOPENGLDRV ON)
    set(BUILD_NMINIGLDRV OFF)
  endif()
  set(BUILD_NOPENGLESDRV OFF)
  if(AMIGA_USE_OPENAL)
    set(BUILD_NOPENALDRV ON)
    set(BUILD_NULLSOUNDDRV OFF)
    set(OPENAL_INCLUDE_DIR "${AMIGA_SDK_ROOT}/include")
    set(OPENAL_LIBRARY "${AMIGA_SDK_ROOT}/lib/libopenal-newgcc.a")
    set(LIBXMP_INCLUDE_DIR "${AMIGA_SDK_ROOT}/include")
    set(LIBXMP_LIBRARY "${AMIGA_SDK_ROOT}/lib/libmodplug.a")
  else()
    set(BUILD_NOPENALDRV OFF)
    set(BUILD_NULLSOUNDDRV ON)
  endif()
  # IpDrv (multiplayer) needs AmiTCP socket headers, whose <exec/types.h>
  # types clash with the engine's in the same TU. Off until that's isolated;
  # single-player does not need it.
  option(BUILD_IPDRV "Build IpDrv (networking)" OFF)
else()
  option(BUILD_IPDRV "Build IpDrv (networking)" ON)
endif()

if(BUILD_STATIC)
  if(MSVC)
    message(FATAL_ERROR "Static mode is not supported with MSVC")
  endif()
  set(LIB_TYPE STATIC)
  add_definitions(-DUNREAL_STATIC)
  message(STATUS "Static mode enabled")
else()
  set(LIB_TYPE SHARED)
endif()

# Determine target arch and get normalized arch string

include(TargetArch)
target_architecture(TARGET_ARCH)

set(TARGET_IS_X86 FALSE)
set(TARGET_IS_ARM FALSE)
set(TARGET_IS_PPC FALSE)
set(TARGET_IS_64BIT FALSE)
set(TARGET_IS_BIG_ENDIAN FALSE)

if(AMIGA)
  # TargetArch can't probe a hunk-format cross compiler; set it directly.
  set(TARGET_ARCH "m68k")
  set(TARGET_IS_BIG_ENDIAN TRUE)
elseif(LINUX_PPC OR TARGET_ARCH MATCHES "^(ppc|powerpc)")
  set(TARGET_ARCH "ppc")
  set(TARGET_IS_PPC TRUE)
  set(TARGET_IS_BIG_ENDIAN TRUE)
elseif(TARGET_ARCH MATCHES "arm")
  set(TARGET_IS_ARM TRUE)
  if(TARGET_ARCH MATCHES "64")
    # arm64/aarch64
    set(TARGET_IS_64BIT TRUE)
    set(TARGET_ARCH "arm64")
  else()
    # arm
    set(TARGET_ARCH "arm")
  endif()
elseif((TARGET_ARCH MATCHES "86") OR (TARGET_ARCH STREQUAL "ia64"))
  set(TARGET_IS_X86 TRUE)
  if(TARGET_ARCH MATCHES "64")
    # x86_64/amd64
    set(TARGET_IS_64BIT TRUE)
    set(TARGET_ARCH "x86_64")
  else()
    # x86
    set(TARGET_ARCH "x86")
  endif()
endif()

message(STATUS "Target arch: ${TARGET_ARCH}")
message(STATUS "64-bit: ${TARGET_IS_64BIT}")

if(TARGET_IS_64BIT)
  if(TARGET_IS_X86 AND NOT MSVC)
    message(STATUS "Building x86 binary with x86_64 compiler")
    set(TARGET_IS_64BIT FALSE)
    set(TARGET_ARCH "x86")
    add_compile_options(-m32)
    add_link_options(-m32)  # linker also needs -m32 or shared libs link as x86-64
  else()
    message(FATAL_ERROR "64-bit platforms are currently not supported. If you're building on Windows with MSVC, try -A Win32.")
  endif()
endif()

# Determine target OS/platform

set(TARGET_IS_WINDOWS FALSE)
set(TARGET_IS_UNIX FALSE)
if(WIN32)
  set(TARGET_IS_WINDOWS TRUE)
else()
  # TODO
  set(TARGET_IS_UNIX TRUE)
endif()

message(STATUS "Windows target: ${TARGET_IS_WINDOWS}")

# Common compiler options

if(TARGET_IS_64BIT)
  add_definitions(-DPLATFORM_64BIT)
else()
  add_definitions(-DPLATFORM_32BIT)
endif()

if(AMIGA)
  add_definitions(-DPLATFORM_M68K)
  # The engine requires signed char (static_assert in UnGcc.h), which is the
  # 68k default. -fno-short-enums keeps enum sizes matching the x86 build.
  add_compile_options(-fno-short-enums)
  add_link_options(-fno-short-enums)
  if(AMIGA_EXACT_LIGHT_INDEX)
    add_definitions(-DAMIGA_EXACT_LIGHT_INDEX=1)
  endif()
  if(AMIGA_PPC_COLOR_ALIASES)
    add_definitions(-DAMIGA_PPC_COLOR_ALIASES=1)
  endif()
  if(AMIGA_PACKED_LIGHT_COORD)
    add_definitions(-DAMIGA_PACKED_LIGHT_COORD=1)
  endif()
  if(AMIGA_PACKED_LIGHT_OS)
    add_definitions(-DAMIGA_PACKED_LIGHT_OS=1)
  endif()
  if(AMIGA_MATCH_LIGHT_CHANNELS)
    add_definitions(-DAMIGA_MATCH_LIGHT_CHANNELS=1)
  endif()
elseif(TARGET_IS_X86)
  add_definitions(-DPLATFORM_X86)
elseif(TARGET_IS_ARM)
  add_definitions(-DPLATFORM_ARM)
  add_compile_options(-fsigned-char -fno-short-enums)
  add_link_options(-fno-short-enums)
  if(VITA)
    add_compile_options(-mno-unaligned-access -fno-optimize-sibling-calls)
    add_link_options(-mno-unaligned-access -Wl,-q)
  endif()
elseif(TARGET_IS_PPC)
  add_definitions(-DPLATFORM_PPC)
  add_compile_options(-mbig-endian -fsigned-char -fno-short-enums)
  add_link_options(-mbig-endian -fno-short-enums)
endif()

if(TARGET_IS_BIG_ENDIAN)
  add_definitions(-DPLATFORM_BIG_ENDIAN)
else()
  add_definitions(-DPLATFORM_LITTLE_ENDIAN)
endif()

if(TARGET_IS_WINDOWS)
  add_definitions(-DPLATFORM_WIN32)
  add_definitions(-DWIN32)
  add_definitions(-DWINDOWS_IGNORE_PACKING_MISMATCH)
else()
  # TODO
  add_definitions(-DPLATFORM_POSIX)
  if(AMIGA)
    # AmigaOS filesystems are case-insensitive, so PLATFORM_CASE_SENSITIVE_FS
    # stays off (which also avoids strcasestr, absent from this libc).
    add_definitions(-DPLATFORM_AMIGA)
    set(CMAKE_EXECUTABLE_SUFFIX "")
  elseif(VITA)
    add_definitions(-DPLATFORM_PSVITA)
  else()
    add_definitions(-DPLATFORM_CASE_SENSITIVE_FS)
    set(CMAKE_EXECUTABLE_SUFFIX ".bin")
  endif()
endif()

if(USE_SDL)
  add_definitions(-DPLATFORM_SDL)
  if(AMIGA OR LINUX_PPC)
    add_definitions(-DPLATFORM_SDL12_COMPAT)
  endif()
endif()

if(MSVC)
  message(STATUS "Building ${CMAKE_BUILD_TYPE} with MSVC")
  add_definitions(-DPLATFORM_MSVC)
  add_compile_options(/nologo /fp:precise /analyze- /MP /MD /Gd /Gy /GS /Gm- /EHsc /Zi /Zc:wchar_t /Zc:inline /Zp4)
  add_compile_options(/wd4291 /wd4297)
  if(TARGET_IS_64BIT)
    set(MSVC_LIBDIR "Win64")
  else()
    set(MSVC_LIBDIR "Win32")
  endif()
else()
  message(STATUS "Building ${CMAKE_BUILD_TYPE} with GCC or Clang")
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-D_DEBUG)
    add_compile_options(-O0)
  else()
    add_compile_options(-O2)
  endif()
  add_compile_options(-ffunction-sections -fno-strict-aliasing)
  add_compile_options(-Wno-write-strings)
  if(AMIGA AND AMIGA_MAX_OPT)
    # Kept opt-in: -O3 and loop unrolling can increase executable size and
    # compile time, while the stable Amiga build remains on -O2.
    add_compile_options(-O3)
  endif()
endif()

# Dependencies

if(USE_SDL)
  if(AMIGA OR LINUX_PPC)
    # SDL 1.2 only. Core/Inc/Amiga holds a SDL2/SDL.h that redirects to the
    # SDL12Compat shim, and must come first on the include path so the
    # engine's #include "SDL2/SDL.h" resolves to it.
    if(NOT DEFINED SDL2_INCLUDE_DIR)
      set(SDL2_INCLUDE_DIR
        "${CMAKE_SOURCE_DIR}/Core/Inc/Amiga"
        "${CMAKE_SOURCE_DIR}/Core/Inc")
      if(AMIGA)
        list(APPEND SDL2_INCLUDE_DIR "${AMIGA_TOOLCHAIN_ROOT}/include/SDL")
      else()
        list(APPEND SDL2_INCLUDE_DIR "${PPC_LINUX_SYSROOT}/usr/include/SDL")
      endif()
    endif()
    if(NOT DEFINED SDL2_LIBRARY)
      # Static engine libraries only need the SDL headers.  The final Unreal
      # executable links SDL together with libGL in one ordered group; adding
      # it as a transitive dependency here puts the same archive several times
      # into the final link and differs from known-good Amiga SDL/GL programs.
      if(AMIGA)
        set(SDL2_LIBRARY "")
      else()
        set(SDL2_LIBRARY "${PPC_LINUX_SYSROOT}/usr/lib/libSDL-1.2.so.0.11.4")
      endif()
    endif()
  elseif(MSVC)
    if(NOT DEFINED SDL2_INCLUDE_DIR)
      set(SDL2_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../Thirdparty/SDL2/include")
    endif()
    if(NOT DEFINED SDL2_LIBRARY)
      set(SDL2_LIBRARY "${CMAKE_SOURCE_DIR}/../Thirdparty/SDL2/lib/${MSVC_LIBDIR}/SDL2.lib")
    endif()
  else()
    # Won't bother with find_package as we're probably cross compiling.
    if(NOT DEFINED SDL2_INCLUDE_DIR)
      set(SDL2_INCLUDE_DIR )
    endif()
    if(NOT DEFINED SDL2_LIBRARY)
      set(SDL2_LIBRARY "-lSDL2")
    endif()
  endif()
endif()

if(BUILD_NOPENALDRV)
  if(MSVC)
    if(NOT DEFINED OPENAL_INCLUDE_DIR)
      set(OPENAL_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../Thirdparty/openal-soft/include")
    endif()
    if(NOT DEFINED OPENAL_LIBRARY)
      set(OPENAL_LIBRARY "${CMAKE_SOURCE_DIR}/../Thirdparty/openal-soft/lib/${MSVC_LIBDIR}/OpenAL32.lib")
    endif()
    if(NOT DEFINED LIBXMP_INCLUDE_DIR)
      set(LIBXMP_INCLUDE_DIR "${CMAKE_SOURCE_DIR}/../Thirdparty/libxmp-lite/include/libxmp-lite")
    endif()
    if(NOT DEFINED LIBXMP_LIBRARY)
      set(LIBXMP_LIBRARY "${CMAKE_SOURCE_DIR}/../Thirdparty/libxmp-lite/lib/${MSVC_LIBDIR}/libxmp-lite.lib")
    endif()
  else()
    # Won't bother with find_package as we're probably cross compiling.
    if(NOT DEFINED OPENAL_INCLUDE_DIR)
      set(OPENAL_INCLUDE_DIR )
    endif()
    if(NOT DEFINED OPENAL_LIBRARY)
      set(OPENAL_LIBRARY "-lopenal")
    endif()
    if(NOT DEFINED LIBXMP_INCLUDE_DIR)
      set(LIBXMP_INCLUDE_DIR )
    endif()
    if(NOT DEFINED LIBXMP_LIBRARY)
      set(LIBXMP_LIBRARY "-lxmp")
    endif()
  endif()
endif()

# Subprojects

set(INSTALL_TARGETS Core Engine Render Fire Unreal)

set(CMAKE_SHARED_LIBRARY_PREFIX "")

add_subdirectory(Core)
add_subdirectory(Engine)
add_subdirectory(Render)
if(BUILD_IPDRV)
  add_subdirectory(IpDrv)
  list(APPEND INSTALL_TARGETS IpDrv)
endif()
add_subdirectory(Fire)

if(USE_SDL)
  # SDL2
  add_subdirectory(NSDLDrv)
  list(APPEND INSTALL_TARGETS NSDLDrv)
else()
  # archaic WinAPI shit
  set(BUILD_WINDRV ON)
  set(BUILD_SOFTDRV ON)
  set(BUILD_NOPENGLDRV OFF)
  set(BUILD_NOPENGLESDRV OFF)
endif()

if(BUILD_WINDRV)
  add_subdirectory(Window)
  add_subdirectory(WinDrv)
  list(APPEND INSTALL_TARGETS Window WinDrv)
endif()

if(BUILD_SOFTDRV)
  add_subdirectory(SoftDrv)
  list(APPEND INSTALL_TARGETS SoftDrv)
endif()

if(BUILD_NOPENGLDRV)
  add_subdirectory(NOpenGLDrv)
  list(APPEND INSTALL_TARGETS NOpenGLDrv)
endif()

if(BUILD_NMINIGLDRV)
  add_subdirectory(NMiniGLDrv)
  list(APPEND INSTALL_TARGETS NMiniGLDrv)
endif()

if(BUILD_NOPENGLESDRV)
  add_subdirectory(NOpenGLESDrv)
  list(APPEND INSTALL_TARGETS NOpenGLESDrv)
endif()

if(BUILD_NULLSOUNDDRV)
  add_subdirectory(SoundDrv)
  list(APPEND INSTALL_TARGETS SoundDrv)
endif()

if(BUILD_NOPENALDRV)
  add_subdirectory(NOpenALDrv)
  list(APPEND INSTALL_TARGETS NOpenALDrv)
endif()

if(BUILD_EDITOR)
  # GUI requires WinDrv
  add_subdirectory(Editor)
  list(APPEND INSTALL_TARGETS Editor)
endif()

add_subdirectory(Unreal)

# assemble all modules and pdbs in one dir

set(INSTALL_FILES )

foreach(TGT ${INSTALL_TARGETS})
  list(APPEND INSTALL_FILES "$<TARGET_FILE:${TGT}>")
  if(MSVC)
    list(APPEND INSTALL_FILES "$<TARGET_PDB_FILE:${TGT}>")
  endif()
endforeach()

install(FILES ${INSTALL_FILES} DESTINATION "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")

if(BUILD_NMINIGLDRV)
  install(FILES
    "${CMAKE_SOURCE_DIR}/../Engine/Localization/NMiniGLDrv.int"
    DESTINATION "${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE}")
endif()
