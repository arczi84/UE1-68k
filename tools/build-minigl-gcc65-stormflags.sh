#!/usr/bin/env bash
set -euo pipefail

ue_root=/mnt/d/dev/UE1-MiniGL
ue_toolchain="${UE_MINIGL_TOOLCHAIN_ROOT:-/opt/amiga}"
ue_minigl="${UE_MINIGL_SDK_ROOT:-/mnt/d/dev/Pistorm3D/Pistorm3D_v12}"
ue_build="${UE_MINIGL_BUILD_DIR:-$ue_root/build/gcc65-minigl-stormflags-060}"
# Match build-amiga-nolerp2-restored-68k code-generation flags. Keep the
# current MiniGL render-timing fix; do not reintroduce its per-polygon cost.
ue_flags='-noixemul -m68060 -mhard-float -fomit-frame-pointer -ffast-math -fno-PIC -fno-pic -DUE_DISABLE_RENDER_TIMING'
# Hardware-tested package read cache; runtime opt-out: -mglreadcache=0.
ue_flags="$ue_flags -DUE_MINIGL_READCACHE"
ue_flags="$ue_flags ${UE_MINIGL_EXTRA_FLAGS:-}"
ue_link="-noixemul -Wl,--strip-all -Wl,-Map=$ue_build/Unreal/Unreal.map"
ue_link="$ue_link ${UE_MINIGL_EXTRA_LINK_FLAGS:-}"

cmake -G Ninja -S "$ue_root/Source" -B "$ue_build" \
  -DCMAKE_TOOLCHAIN_FILE="$ue_root/Source/cmake/AmigaOS68k.cmake" \
  "-DAMIGA_TOOLCHAIN_ROOT=$ue_toolchain" -DAMIGA_NATIVE_CXX_STDLIB=ON \
  -DAMIGA_SDK_ROOT=/mnt/d/amiga-gcc2 -DAMIGA_CPU=68060 \
  -DAMIGA_HARD_FLOAT=ON -DAMIGA_GL_BACKEND=MiniGL \
  "-DAMIGA_SHARED_MINIGL_ROOT=$ue_minigl" \
  -DCMAKE_BUILD_TYPE=Release -DAMIGA_MAX_OPT=ON -DUE_MGL_CAPTURE=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DBUILD_IPDRV=OFF -DAMIGA_USE_OPENAL=OFF \
  "-DSDL2_INCLUDE_DIR=$ue_root/Source/Core/Inc/Amiga;$ue_root/Source/Core/Inc;/mnt/d/amiga-gcc2/include/SDL" \
  "-DCMAKE_C_FLAGS=$ue_flags" "-DCMAKE_CXX_FLAGS=$ue_flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$ue_link"
cmake --build "$ue_build" --target Unreal -j16
file "$ue_build/Unreal/Unreal"

# Deployment is separate so existing executables are never overwritten here.
