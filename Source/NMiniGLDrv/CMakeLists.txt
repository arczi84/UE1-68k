project(NMiniGLDrv C CXX)

if(NOT AMIGA)
  message(FATAL_ERROR "NMiniGLDrv is available only for AmigaOS.")
endif()

if(NOT USE_SDL)
  message(FATAL_ERROR "NMiniGLDrv requires NSDLDrv to function.")
endif()

if(NOT EXISTS "${AMIGA_SHARED_MINIGL_ROOT}/include/proto/minigl.h")
  message(FATAL_ERROR "Shared MiniGL headers not found under ${AMIGA_SHARED_MINIGL_ROOT}/include")
endif()

if(NOT EXISTS "${AMIGA_SHARED_MINIGL_ROOT}/libminigl_dispatch.a")
  message(FATAL_ERROR "MiniGL dispatch import library not found at ${AMIGA_SHARED_MINIGL_ROOT}/libminigl_dispatch.a")
endif()

set(SRC_FILES "NMiniGLDrv.cpp")

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${AMIGA_SHARED_MINIGL_ROOT}/include
  ${SDL2_INCLUDE_DIR}
  ${AMIGA_SDK_ROOT}/include
)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} Render Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE
  NOPENGLDRV_USE_MINIGL
  NOPENGLDRV_USE_SHARED_MINIGL
  NOPENGLDRV_EXPORTS
  UPACKAGE_NAME=${PROJECT_NAME}
)
