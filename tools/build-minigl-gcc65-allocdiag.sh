#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_build="${UE_MINIGL_BUILD_DIR:-$ue_root/build/gcc65-minigl-o2-allocdiag}"
# Exceptions are required by UE1. The user approved omitting -fno-exceptions.
ue_flags='-mcpu=68060 -mhard-float -O2 -DNDEBUG -mcrt=nix20 -fno-fast-math -fno-strict-aliasing -fomit-frame-pointer -fno-optimize-sibling-calls -funsafe-loop-optimizations -ftree-loop-if-convert-stores -fno-PIC -fno-pic -DUE_DISABLE_RENDER_TIMING -DUE_ALLOC_DIAG'
cmake -G Ninja -S "$ue_root/Source" -B "$ue_build" \
  -DCMAKE_TOOLCHAIN_FILE="$ue_root/Source/cmake/AmigaOS68k.cmake" \
  -DAMIGA_TOOLCHAIN_ROOT=/opt/amiga -DAMIGA_SDK_ROOT=/mnt/d/amiga-gcc2 \
  -DAMIGA_CPU=68060 -DAMIGA_GL_BACKEND=MiniGL \
  -DAMIGA_SHARED_MINIGL_ROOT=/mnt/d/dev/Pistorm3D/Pistorm3D_v12 \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_C_FLAGS_RELEASE='-O2 -DNDEBUG' \
  -DCMAKE_CXX_FLAGS_RELEASE='-O2 -DNDEBUG' -DAMIGA_MAX_OPT=OFF \
  -DUE_MGL_CAPTURE=OFF -DBUILD_IPDRV=OFF -DAMIGA_USE_OPENAL=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  "-DSDL2_INCLUDE_DIR=$ue_root/Source/Core/Inc/Amiga;$ue_root/Source/Core/Inc;/mnt/d/amiga-gcc2/include/SDL" \
  "-DCMAKE_C_FLAGS=$ue_flags" "-DCMAKE_CXX_FLAGS=$ue_flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=-mcpu=68060 -mhard-float -mcrt=nix20 -Wl,--strip-all -Wl,-Map=$ue_build/Unreal/Unreal.map"
cmake --build "$ue_build" --target Unreal -j16
file "$ue_build/Unreal/Unreal"
