#!/usr/bin/env bash
set -euo pipefail

ue_root=/mnt/d/dev/UE1-MiniGL
ue_build="$ue_root/build/gcc16-rc9-modefix-minigl-fast040"
ue_flags='-B/home/arczi/toolchains/amiga-gcc16-modefix/ -mcrt=nix20 -m68040 -O3 -ffast-math -fno-unroll-loops -fomit-frame-pointer -fno-optimize-sibling-calls -fno-strict-aliasing -ffunction-sections -fdata-sections -fno-PIC -fno-pic'
ue_link="-B/home/arczi/toolchains/amiga-gcc16-modefix/ -m68040 -mcrt=nix20 -Wl,--strip-all -Wl,-Map=$ue_build/Unreal/Unreal.map -Wl,--defsym=__Znwj=__Znwm -Wl,--defsym=__Znaj=__Znam -Wl,--defsym=__ZdlPvj=__ZdlPvm -Wl,--defsym=__ZdaPvj=__ZdaPvm"

cmake -G Ninja -S "$ue_root/Source" -B "$ue_build" \
  -DCMAKE_TOOLCHAIN_FILE="$ue_root/Source/cmake/AmigaOS68k.cmake" \
  -DAMIGA_TOOLCHAIN_ROOT=/opt/amiga-gcc16-rc9 \
  -DAMIGA_SDK_ROOT=/mnt/d/amiga-gcc2 -DAMIGA_CPU=68040 \
  -DAMIGA_GL_BACKEND=MiniGL \
  -DAMIGA_SHARED_MINIGL_ROOT=/mnt/d/dev/Pistorm3D/Pistorm3D_v12 \
  -DCMAKE_BUILD_TYPE=Release -DAMIGA_MAX_OPT=ON -DUE_MGL_CAPTURE=OFF \
  -DBUILD_IPDRV=OFF -DAMIGA_USE_OPENAL=OFF \
  "-DSDL2_INCLUDE_DIR=$ue_root/Source/Core/Inc/Amiga;$ue_root/Source/Core/Inc;/mnt/d/amiga-gcc2/include/SDL" \
  "-DCMAKE_C_FLAGS=$ue_flags" "-DCMAKE_CXX_FLAGS=$ue_flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$ue_link"
cmake --build "$ue_build" --target Unreal -j16

# No automatic deployment: inspect the artifact before copying to the game.
file "$ue_build/Unreal/Unreal"
