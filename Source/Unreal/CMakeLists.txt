project(Unreal)

if(USE_SDL)
  set(SRC_FILES "Src/SDLLaunch.cpp")
else()
  set(SRC_FILES "Src/WinLaunch.cpp")
endif()

if(AMIGA)
  # Large swapped stack for UE's deep recursion (see AmigaStack.c). Kept a .c
  # file so exec/types.h doesn't collide with the engine's BYTE/WORD typedefs.
  list(APPEND SRC_FILES "Src/AmigaStack.c" "Src/AmigaRawMouse.c")
  if(AMIGA_GL_BACKEND STREQUAL "MiniGL")
    list(APPEND SRC_FILES "Src/SDLAmigaNoGL.c")
  endif()

  # GCC 6's stock gprof sampler is tied to the chipset VBlank interrupt.  RTG
  # systems can therefore produce call counts but an empty time histogram.
  # Keep the replacement strictly opt-in so the normal executable is untouched.
  option(AMIGA_GPROF_TIMER "Use the RTG-safe Amiga gprof sampler" OFF)
  if(AMIGA_GPROF_TIMER)
    list(APPEND SRC_FILES "Src/AmigaGprof.c")
    # This file implements mcount itself, so compiling it with -pg would make
    # mcount recursively call mcount.
    set_source_files_properties("Src/AmigaGprof.c"
      PROPERTIES COMPILE_OPTIONS "-fno-profile")
  endif()
endif()

if(VITA)
  set(VITA_APP_NAME "Unreal")
  set(VITA_TITLEID  "UNRL00001")
  set(VITA_MKSFOEX_FLAGS "-d ATTRIBUTE2=12")
  include("${VITASDK}/share/vita.cmake" REQUIRED)
endif()

if(TARGET_IS_WINDOWS)
  list(APPEND SRC_FILES "Src/Res/LaunchRes.rc")
  add_executable(${PROJECT_NAME} WIN32 ${SRC_FILES})
else()
  add_executable(${PROJECT_NAME} ${SRC_FILES})
endif()

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
)

if(USE_SDL)
  target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} NSDLDrv)
  target_include_directories(${PROJECT_NAME} PRIVATE ${SDL2_INCLUDE_DIR})
elseif(TARGET_IS_WINDOWS)
  target_link_libraries(${PROJECT_NAME} Window)
else()
  message(FATAL_ERROR "Non-Windows platforms can only use NSDLDrv.")
endif()

if(BUILD_STATIC)
  set(LINK_PACKAGES ${INSTALL_TARGETS})
  list(REMOVE_ITEM LINK_PACKAGES ${PROJECT_NAME})
  target_link_libraries(${PROJECT_NAME} -Wl,--whole-archive ${LINK_PACKAGES} -Wl,--no-whole-archive)
else()
  target_link_libraries(${PROJECT_NAME} Engine Core)
endif()

if(AMIGA)
  if(NOT AMIGA_GL_BACKEND STREQUAL "MiniGL")
  # The stock Amiga SDL 1.2 CGX backend drops SDL_FULLSCREEN when
  # BestCModeIDTags cannot see a board's 16-bit modes.  VideoCore exposes
  # RGB16PC modes and triggers exactly that path.  Build the two patched CGX
  # objects and replace only those members in a private copy of SDL; the SDK's
  # original library remains untouched.
  set(SDL_AMIGA_SOURCE_DIR
      "${CMAKE_SOURCE_DIR}/ThirdParty/SDL12GL")
  set(SDL_AMIGA_FIX_DIR "${CMAKE_BINARY_DIR}/SDLAmigaVideoFix")
  set(SDL_AMIGA_FIX_LIB "${SDL_AMIGA_FIX_DIR}/libSDL_libnix_ue1.a")
  set(SDL_AMIGA_FIX_FLAGS
      -I${AMIGA_TOOLCHAIN_ROOT}/include
      -I${SDL_AMIGA_SOURCE_DIR}
      -I${SDL_AMIGA_SOURCE_DIR}/include
      -I${SDL_AMIGA_SOURCE_DIR}/include/SDL
      -I${AMIGA_SDK_ROOT}/include
      -O3 -fomit-frame-pointer -m${AMIGA_CPU} -mhard-float -ffast-math
      -noixemul -fno-PIC -fno-pic
      -DNOIXEMUL -D_HAVE_STDINT_H -DNO_AMIGADEBUG
      -DENABLE_CYBERGRAPHICS -DNO_SIGNAL_H -D__MEM_AMIGA -DENABLE_AHI
      # Must match the ABI of libSDL_libnix.a.  Its SDL_VideoDevice contains
      # the SDL 1.2 OpenGL procedure table even when Unreal uses SoftDrv.
      -DSDL_VIDEO_OPENGL=1)

  add_custom_command(
    OUTPUT "${SDL_AMIGA_FIX_LIB}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDL_AMIGA_FIX_DIR}"
    COMMAND "${CMAKE_C_COMPILER}" ${SDL_AMIGA_FIX_FLAGS}
            -c "${SDL_AMIGA_SOURCE_DIR}/video/amigaos/SDL_cgxvideo.c"
            -o "${SDL_AMIGA_FIX_DIR}/SDL_cgxvideo.go"
    COMMAND "${CMAKE_C_COMPILER}" ${SDL_AMIGA_FIX_FLAGS}
            -c "${SDL_AMIGA_SOURCE_DIR}/video/amigaos/SDL_cgxmodes.c"
            -o "${SDL_AMIGA_FIX_DIR}/SDL_cgxmodes.go"
    COMMAND "${CMAKE_COMMAND}" -E copy
            "${AMIGA_SDK_ROOT}/lib/libSDL_libnix.a" "${SDL_AMIGA_FIX_LIB}"
    COMMAND "${CMAKE_AR}" r "${SDL_AMIGA_FIX_LIB}"
            "${SDL_AMIGA_FIX_DIR}/SDL_cgxvideo.go"
            "${SDL_AMIGA_FIX_DIR}/SDL_cgxmodes.go"
    COMMAND "${CMAKE_RANLIB}" "${SDL_AMIGA_FIX_LIB}"
    DEPENDS
      "${SDL_AMIGA_SOURCE_DIR}/video/amigaos/SDL_cgxvideo.c"
      "${SDL_AMIGA_SOURCE_DIR}/video/amigaos/SDL_cgxmodes.c"
      "${SDL_AMIGA_SOURCE_DIR}/video/amigaos/SDL_cgxgl_c.h"
      "${SDL_AMIGA_SOURCE_DIR}/video/SDL_sysvideo.h"
      "${AMIGA_SDK_ROOT}/lib/libSDL_libnix.a"
    VERBATIM)
  add_custom_target(SDLAmigaVideoFix DEPENDS "${SDL_AMIGA_FIX_LIB}")
  add_dependencies(${PROJECT_NAME} SDLAmigaVideoFix)
  endif()

  # Custom linker script: gathers C++ global constructors into a dense
  # [__UE_CTOR_START, __UE_CTOR_END) array that main() runs itself, since
  # libnix's crt never walks the constructor list. -T replaces the default
  # script, so amiga68k.ld is a copy of it plus the CTOR bracketing.
  target_link_options(${PROJECT_NAME} PRIVATE
    "-T${CMAKE_SOURCE_DIR}/cmake/amiga68k.ld")
  set_property(TARGET ${PROJECT_NAME} APPEND PROPERTY LINK_DEPENDS
    "${CMAKE_SOURCE_DIR}/cmake/amiga68k.ld")

  if(AMIGA_GL_BACKEND STREQUAL "MiniGL")
    # SDL remains responsible for joystick/audio, but its stock archive also
    # embeds the SDL/StormMesa GL bridge.  Remove only those two members from
    # a private copy so SDL can never create or own the MiniGL context.
    set(SDL_AMIGA_NOGL_DIR "${CMAKE_BINARY_DIR}/SDLAmigaNoGL")
    set(SDL_AMIGA_NOGL_LIB "${SDL_AMIGA_NOGL_DIR}/libSDL_libnix_nogl.a")
    add_custom_command(
      OUTPUT "${SDL_AMIGA_NOGL_LIB}"
      COMMAND "${CMAKE_COMMAND}" -E make_directory "${SDL_AMIGA_NOGL_DIR}"
      COMMAND "${CMAKE_COMMAND}" -E copy
        "${AMIGA_SDK_ROOT}/lib/libSDL_libnix.a" "${SDL_AMIGA_NOGL_LIB}"
      COMMAND "${CMAKE_AR}" d "${SDL_AMIGA_NOGL_LIB}"
        SDL_cgxgl.go SDL_cgxgl_wrapper.go
      COMMAND "${CMAKE_RANLIB}" "${SDL_AMIGA_NOGL_LIB}"
      DEPENDS "${AMIGA_SDK_ROOT}/lib/libSDL_libnix.a"
      VERBATIM)
    add_custom_target(SDLAmigaNoGL DEPENDS "${SDL_AMIGA_NOGL_LIB}")
    add_dependencies(${PROJECT_NAME} SDLAmigaNoGL)

    target_compile_definitions(${PROJECT_NAME} PRIVATE
      AMIGA_USE_MINIGL
      AMIGA_USE_SHARED_MINIGL
      AMIGA_USE_NATIVE_MINIGL)
    target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
      "${AMIGA_SHARED_MINIGL_ROOT}/include")
    target_link_libraries(${PROJECT_NAME}
      "-L${AMIGA_SDK_ROOT}/lib"
      -Wl,--start-group
      "${SDL_AMIGA_NOGL_LIB}"
      "${AMIGA_SHARED_MINIGL_ROOT}/libminigl_dispatch.a"
      "${AMIGA_SDK_ROOT}/lib/libSDL_mixer.a"
      "${AMIGA_SDK_ROOT}/lib/libmodplug.a"
      -Wl,--end-group
      -lpthread
      -lm
      -ldebug
    )
  else()
    target_link_options(${PROJECT_NAME} PRIVATE
      "-Wl,--wrap=AmigaMesaCreateContext")
    # libSDL_libnix.a has built-in GL support (SDL_cgxgl) that references
    # AmigaMesa*/gl* from libGL, and libGL in turn needs SDL's context glue.
    target_link_libraries(${PROJECT_NAME}
      "-L${AMIGA_SDK_ROOT}/lib"
      -Wl,--start-group "${SDL_AMIGA_FIX_LIB}" -lGL -Wl,--end-group
      -lpthread
      -lm
      -ldebug
    )
  endif()
endif()

if(VITA)
  target_link_libraries(${PROJECT_NAME}
    -Wl,--whole-archive pthread -Wl,--no-whole-archive
    openal
    xmp
    vitaGL
    vitashark
    mathneon
    SceShaccCgExt
    taihen_stub
    SceNet_stub
    SceNetCtl_stub
    SceAppMgr_stub
    SceAppUtil_stub
    SceAudio_stub
    SceAudioIn_stub
    SceCtrl_stub
    SceCommonDialog_stub
    SceDisplay_stub
    SceFios2_stub
    SceGxm_stub
    SceIme_stub
    SceShaccCg_stub
    SceSysmodule_stub
    ScePower_stub
    SceTouch_stub
    SceMotion_stub
    SceHid_stub
    SceVshBridge_stub
    SceSblSsMgr_stub
    SceKernelDmacMgr_stub
  )
endif()

target_compile_definitions(${PROJECT_NAME} PRIVATE UNREAL_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

if(VITA)
  vita_create_self(eboot.bin ${PROJECT_NAME} UNSAFE)
  vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE ${CMAKE_CURRENT_SOURCE_DIR}/Res/sce_sys sce_sys
  )
endif()
