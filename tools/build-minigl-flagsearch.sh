#!/usr/bin/env bash
# Flag search build of the v29 source (with 64 KiB package buffer).
# Env: UE_TC (toolchain root), UE_CRT (-noixemul | -mcrt=nix20),
#      UE_LEVEL (-O level digit), UE_OPT (-f flags; explicit -f wins over -O
#      regardless of order), UE_INTERPRETER_OPT,
#      UE_NAME (build dir suffix), UE_CPU, UE_EXTRA_FLAGS, UE_EXTRA_CMAKE,
#      UE_STRIP (--strip-debug keeps symbols for the sampling profiler),
#      UE_ROOT (source checkout; default UE1-MiniGL), UE_BUILD_ROOT (where
#      fs-NAME / sdk-fs-* go; default UE_ROOT/build), UE_SDK_TEMPLATE (v29
#      SDK include tree prepared by prepare-minigl-v29-sdk.sh).
set -euo pipefail
ue_root=${UE_ROOT:-/mnt/d/dev/UE1-MiniGL}
ue_build_root=${UE_BUILD_ROOT:-$ue_root/build}
ue_sdk_template=${UE_SDK_TEMPLATE:-/mnt/d/dev/UE1-MiniGL/build/sdk-minigl-v29-gcc65}
tc=${UE_TC:-/opt/amiga-gcc65-250215}
crt=${UE_CRT:--noixemul}
opt=${UE_OPT:--ffast-math}
level=${UE_LEVEL:-3}
cpu=${UE_CPU:-68060}
name=${UE_NAME:?set UE_NAME}
ue_src="${UE_SOURCE:-$ue_root/build/src-v29-lightscale/Source}"
ue_build="$ue_build_root/fs-$name"
ue_sdk="$ue_build_root/sdk-fs-$(basename "$tc")${crt// /}-$cpu"
mkdir -p "$ue_sdk/libraryroot" "$ue_sdk/backend" "$ue_sdk/obj"
cp -a "$ue_sdk_template/libraryroot/include" "$ue_sdk/libraryroot/"
cp -a "$ue_sdk_template/backend/include" "$ue_sdk/backend/"
ue_vendor=/mnt/d/dev/Pistorm3D/MiniGL_Classic_v29.1
"$tc/bin/m68k-amigaos-gcc" $crt -m$cpu -mhard-float -O2 -g0 -I"$ue_sdk/libraryroot/include" \
  -c "$ue_vendor/libraryroot/client/minigl_base.c" -o "$ue_sdk/obj/minigl_base.o"
"$tc/bin/m68k-amigaos-gcc" $crt -m$cpu -mhard-float -O2 -g0 -I"$ue_sdk/libraryroot/include" \
  -c "$ue_root/Source/NMiniGLDrv/MiniGLClient.c" -o "$ue_sdk/obj/minigl_open.o"
"$tc/bin/m68k-amigaos-gcc" $crt -m$cpu -c \
  "$ue_vendor/libraryroot/client/minigl_dispatch_stub_gcc.s" -o "$ue_sdk/obj/stub.o"
rm -f "$ue_sdk/libraryroot/libminigl_dispatch.a"
"$tc/bin/m68k-amigaos-ar" rcs "$ue_sdk/libraryroot/libminigl_dispatch.a" "$ue_sdk/obj/"*.o
ue_flags="$crt -m$cpu -mhard-float -fomit-frame-pointer -fno-PIC -fno-pic -DUE_DISABLE_RENDER_TIMING -DUE_MINIGL_HASHCACHE -DUE_MINIGL_READCACHE -DUE_MINIGL_STABLE_BASELINE -g0 -DUE_RELEASE_PACKAGE $opt ${UE_EXTRA_FLAGS:-}"
ue_link="$crt -m$cpu -mhard-float -Wl,${UE_STRIP:---strip-all} -Wl,-Map=$ue_build/Unreal/Unreal.map"
if [[ $("$tc/bin/m68k-amigaos-g++" -dumpversion) == 6* ]]; then
  ue_link="$ue_link -L/opt/amiga-gcc65-250215/m68k-amigaos/lib"
else
  ue_link="$ue_link -Wl,--defsym=__Znwj=__Znwm -Wl,--defsym=__Znaj=__Znam -Wl,--defsym=__ZdlPvj=__ZdlPvm -Wl,--defsym=__ZdaPvj=__ZdaPvm"
fi
cmake -G Ninja -S "$ue_src" -B "$ue_build" \
  -DCMAKE_TOOLCHAIN_FILE="$ue_src/cmake/AmigaOS68k.cmake" \
  -DAMIGA_TOOLCHAIN_ROOT="$tc" -DAMIGA_NATIVE_CXX_STDLIB=ON \
  -DAMIGA_SDK_ROOT=/mnt/d/amiga-gcc2 -DAMIGA_CPU=$cpu -DAMIGA_HARD_FLOAT=ON \
  -DAMIGA_GL_BACKEND=MiniGL -DAMIGA_SHARED_MINIGL_ROOT="$ue_sdk/libraryroot" \
  -DCMAKE_BUILD_TYPE=Release -DAMIGA_MAX_OPT=OFF -DUE_MGL_CAPTURE=OFF \
  -DBUILD_IPDRV=OFF -DAMIGA_USE_OPENAL=OFF ${UE_EXTRA_CMAKE:-} \
  "-DUE_INTERPRETER_OPT=${UE_INTERPRETER_OPT:--Og}" "-DUE_BASE_OPT_LEVEL=$level" \
  "-DCMAKE_C_FLAGS_RELEASE=-DNDEBUG" "-DCMAKE_CXX_FLAGS_RELEASE=-DNDEBUG" \
  "-DSDL2_INCLUDE_DIR=$ue_src/Core/Inc/Amiga;$ue_src/Core/Inc;/mnt/d/amiga-gcc2/include/SDL" \
  "-DCMAKE_C_FLAGS=$ue_flags" "-DCMAKE_CXX_FLAGS=$ue_flags" "-DCMAKE_EXE_LINKER_FLAGS=$ue_link"
cmake --build "$ue_build" --target Unreal -j16
