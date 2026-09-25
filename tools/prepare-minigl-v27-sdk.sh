#!/usr/bin/env bash
set -euo pipefail
: "${UE_MINIGL_TOOLCHAIN_ROOT:?Select the compiler first}"
: "${UE_MINIGL_SDK_ROOT:?Select a local SDK build directory first}"
ue_v27=/mnt/d/dev/Pistorm3D/MiniGL_Classic_v27
ue_cc="$UE_MINIGL_TOOLCHAIN_ROOT/bin/m68k-amigaos-gcc"
ue_ar="$UE_MINIGL_TOOLCHAIN_ROOT/bin/m68k-amigaos-ar"
mkdir -p "$UE_MINIGL_SDK_ROOT/include/proto" "$UE_MINIGL_SDK_ROOT/include/clib" "$UE_MINIGL_SDK_ROOT/obj"
cp -a "$ue_v27/include/libraries" "$UE_MINIGL_SDK_ROOT/include/"
cp "$ue_v27/include/proto/minigl.h" "$UE_MINIGL_SDK_ROOT/include/proto/"
cp "$ue_v27/include/clib/minigl_protos.h" "$UE_MINIGL_SDK_ROOT/include/clib/"
# Match Makefile_dispatch_classic.gcc: Classic mglQ3 headers precede the
# dispatch headers (the bundle's generic mgl headers refer to V3D internals).
cp -a "$ue_v27/mglQ3/include/mgl" "$ue_v27/mglQ3/include/Warp3D" "$UE_MINIGL_SDK_ROOT/include/"
# Do not shadow the compiler's OS headers with bundled cybergraphics/ahi/etc.
# Build the import glue from the SAME v27 SDK, not the old v12 archive.
for ue_unit in minigl_base minigl_open; do
  ue_source="$ue_v27/libraryroot/client/$ue_unit.c"
  if [[ "$ue_unit" == minigl_open ]]; then
    ue_source=/mnt/d/dev/UE1-MiniGL/Source/NMiniGLDrv/MiniGLClient.c
  fi
  "$ue_cc" -noixemul -m68060 -mhard-float -O2 -g0 -fno-strict-aliasing \
    -I"$UE_MINIGL_SDK_ROOT/include" \
    -c "$ue_source" \
    -o "$UE_MINIGL_SDK_ROOT/obj/$ue_unit.o"
done
"$ue_cc" -m68060 -c "$ue_v27/libraryroot/client/minigl_dispatch_stub_gcc.s" \
  -o "$UE_MINIGL_SDK_ROOT/obj/minigl_dispatch_stub_gcc.o"
"$ue_ar" rcs "$UE_MINIGL_SDK_ROOT/libminigl_dispatch.a" \
  "$UE_MINIGL_SDK_ROOT/obj/minigl_base.o" \
  "$UE_MINIGL_SDK_ROOT/obj/minigl_open.o" \
  "$UE_MINIGL_SDK_ROOT/obj/minigl_dispatch_stub_gcc.o"
# Compile-time ABI assertions: v27 table size 632 and all published offsets.
"$ue_cc" -noixemul -m68060 -I"$UE_MINIGL_SDK_ROOT/include" \
  -c "$ue_v27/libraryroot/tests/test_dispatch_abi.c" \
  -o "$UE_MINIGL_SDK_ROOT/obj/test_dispatch_abi.o"
