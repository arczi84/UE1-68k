project(Unreal)

if(USE_SDL)
  set(SRC_FILES "Src/SDLLaunch.cpp")
else()
  set(SRC_FILES "Src/WinLaunch.cpp")
endif()

if(AMIGA)
  # Large swapped stack for UE's deep recursion (see AmigaStack.c). Kept a .c
  # file so exec/types.h doesn't collide with the engine's BYTE/WORD typedefs.
  list(APPEND SRC_FILES "Src/AmigaStack.c")
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
  # Custom linker script: gathers C++ global constructors into a dense
  # [__UE_CTOR_START, __UE_CTOR_END) array that main() runs itself, since
  # libnix's crt never walks the constructor list. -T replaces the default
  # script, so amiga68k.ld is a copy of it plus the CTOR bracketing.
  target_link_options(${PROJECT_NAME} PRIVATE
    "-T${CMAKE_SOURCE_DIR}/cmake/amiga68k.ld"
    "-Wl,--wrap=AmigaMesaCreateContext")

  # libSDL_libnix.a has built-in GL support (SDL_cgxgl) that references
  # AmigaMesa*/gl* from libGL, and libGL in turn needs SDL's context glue, so
  # the two archives are mutually dependent: wrap them in a link group so ld
  # resolves across them in one pass. -ldebug supplies kprintf (referenced by
  # libSDL_libnix).
  target_link_libraries(${PROJECT_NAME}
    "-L${AMIGA_SDK_ROOT}/lib"
    -Wl,--start-group -lSDL_libnix -lGL -Wl,--end-group
    -lpthread
    -lm
    -ldebug
  )
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
