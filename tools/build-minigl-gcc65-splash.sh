#!/usr/bin/env bash
# Release build of this checkout with the 1.4.1 recipe: GCC 6.5.0b 250215,
# -noixemul, -O3 base, -ffast-math, function sections off, MiniGL v29.1.
set -euo pipefail
here=$(cd "$(dirname "$0")/.." && pwd)
export UE_ROOT="$here" UE_SOURCE="$here/Source" UE_BUILD_ROOT="$here/build"
export UE_TC=/opt/amiga-gcc65-250215 UE_CRT=-noixemul UE_LEVEL=3 UE_OPT=-ffast-math
export UE_NAME=${UE_NAME:-gcc65-release}
export UE_EXTRA_CMAKE="-DUE_FUNCTION_SECTIONS=OFF ${UE_EXTRA_CMAKE:-}"
banner=$("$UE_TC/bin/m68k-amigaos-g++" --version | head -1)
[[ "$banner" == *"6.5.0b 250215183648"* ]] || { echo "Wrong compiler: $banner" >&2; exit 1; }
exec bash "$here/tools/build-minigl-flagsearch.sh"
