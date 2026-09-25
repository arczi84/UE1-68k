project(NSDLDrv CXX)

set(SRC_FILES
  "Src/NSDLViewport.cpp"
  "Src/NSDLClient.cpp"
  "Src/NSDLDrv.cpp"
)

if(AMIGA AND AMIGA_GL_BACKEND STREQUAL "MiniGL")
  list(APPEND SRC_FILES "Src/AmigaMiniGLWindow.c")
  list(APPEND SRC_FILES "Src/AmigaMiniGLBufferProbe.c")
  option(UE_MGL_CAPTURE "Record native MiniGL calls in a separate diagnostic build" OFF)
  if(UE_MGL_CAPTURE)
    list(APPEND SRC_FILES "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/minigl-replay/trace.c")
  endif()
endif()

add_library(${PROJECT_NAME} ${LIB_TYPE} ${SRC_FILES})

if(UE_MGL_CAPTURE)
  target_compile_definitions(${PROJECT_NAME} PRIVATE MGL_CAPTURE)
endif()

target_include_directories(${PROJECT_NAME}
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/Inc
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/Src
  ${SDL2_INCLUDE_DIR}
)

target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARY} Engine Core)

target_compile_definitions(${PROJECT_NAME} PRIVATE NSDLDRV_EXPORTS UPACKAGE_NAME=${PROJECT_NAME})

if(AMIGA AND AMIGA_GL_BACKEND STREQUAL "MiniGL")
  target_compile_definitions(${PROJECT_NAME} PRIVATE
    NSDLDRV_USE_MINIGL
    AMIGA_USE_NATIVE_MINIGL)
  target_include_directories(${PROJECT_NAME} BEFORE PRIVATE
    "${AMIGA_SHARED_MINIGL_ROOT}/include")
  target_include_directories(${PROJECT_NAME} PRIVATE
    "${AMIGA_SDK_ROOT}/include")
endif()
