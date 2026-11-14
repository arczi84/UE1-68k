#!/usr/bin/env bash
# Same flags as build-minigl-gcc65-splash.sh (the 1.4.1 recipe) but with
# AmigaDev GCC 16.2-rc24 and its libnix (-mcrt=nix20, the -noixemul
# equivalent). The script adds the size_t operator new/delete aliases.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
export UE_ROOT="$here" UE_SOURCE="$here/Source" UE_BUILD_ROOT="$here/build"
export UE_TC=/opt/amiga-gcc16-rc24 UE_CRT=-mcrt=nix20 UE_LEVEL=3 UE_OPT=-ffast-math
export UE_EXTRA_CMAKE="-DUE_FUNCTION_SECTIONS=OFF ${UE_EXTRA_CMAKE:-}"
# UE_LTO=1: whole-program LTO. Objects are fat because the toolchain file
# pins the plain ar (no LTO plugin, so no archive index for slim objects);
# the code flags are repeated at link time.
if [[ "${UE_LTO:-0}" == 1 ]]; then
  export UE_NAME=${UE_NAME:-gcc16-rc24-lto}
  export UE_EXTRA_FLAGS="-flto=16 -ffat-lto-objects ${UE_EXTRA_FLAGS:-}"
  export UE_EXTRA_LINK_FLAGS="-flto=16 -O3 -ffast-math -fomit-frame-pointer ${UE_EXTRA_LINK_FLAGS:-}"
fi
export UE_NAME=${UE_NAME:-gcc16-rc24-release}
banner=$("$UE_TC/bin/m68k-amigaos-g++" --version | head -1)
[[ "$banner" == *"16.2-rc24"* ]] || { echo "Wrong compiler: $banner" >&2; exit 1; }
exec bash "$here/tools/build-minigl-flagsearch.sh"
