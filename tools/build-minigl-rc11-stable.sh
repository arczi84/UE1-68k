#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_build="${UE_RC11_BUILD_DIR:-$ue_root/build/gcc16-rc11-stable}"
ue_flags='-mcrt=nix20 -m68060 -mhard-float -fomit-frame-pointer -ffast-math -fno-PIC -fno-pic -DUE_DISABLE_RENDER_TIMING -DUE_MINIGL_HASHCACHE -DUE_MINIGL_READCACHE -DUE_MINIGL_STABLE_BASELINE'
ue_flags="$ue_flags ${UE_RC11_EXTRA_FLAGS:-}"
ue_link="-mcrt=nix20 -m68060 -mhard-float -Wl,--strip-all -Wl,-Map=$ue_build/Unreal/Unreal.map -Wl,--defsym=__Znwj=__Znwm -Wl,--defsym=__Znaj=__Znam -Wl,--defsym=__ZdlPvj=__ZdlPvm -Wl,--defsym=__ZdaPvj=__ZdaPvm"
cmake -G Ninja -S "$ue_root/Source" -B "$ue_build" \
  -DCMAKE_TOOLCHAIN_FILE="$ue_root/Source/cmake/AmigaOS68k.cmake" \
  -DAMIGA_TOOLCHAIN_ROOT=/opt/amiga-gcc16-rc11 \
  -DAMIGA_CXX_STDLIB_ROOT=/opt/amiga-gcc16-rc11/lib/gcc/m68k-amigaos/16.2.0b \
  -DAMIGA_SDK_ROOT=/mnt/d/amiga-gcc2 -DAMIGA_CPU=68060 -DAMIGA_HARD_FLOAT=ON \
  -DAMIGA_GL_BACKEND=MiniGL \
  -DAMIGA_SHARED_MINIGL_ROOT=/mnt/d/dev/Pistorm3D/Pistorm3D_v12 \
  -DCMAKE_BUILD_TYPE=Release -DAMIGA_MAX_OPT=ON -DUE_MGL_CAPTURE=OFF \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_IPDRV=OFF -DAMIGA_USE_OPENAL=OFF \
  "-DSDL2_INCLUDE_DIR=$ue_root/Source/Core/Inc/Amiga;$ue_root/Source/Core/Inc;/mnt/d/amiga-gcc2/include/SDL" \
  "-DCMAKE_C_FLAGS=$ue_flags" "-DCMAKE_CXX_FLAGS=$ue_flags" \
  "-DCMAKE_EXE_LINKER_FLAGS=$ue_link"
cmake --build "$ue_build" --target Unreal -j16
file "$ue_build/Unreal/Unreal"
