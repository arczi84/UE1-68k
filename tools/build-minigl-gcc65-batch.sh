#!/usr/bin/env bash
set -euo pipefail
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc65-minigl-o3-batch
export UE_MINIGL_EXTRA_FLAGS=-DUE_MINIGL_HASHCACHE
# New /opt/amiga lacks SDL's debug support archive. Supply only that archive,
# without adding another compiler's C/C++ runtime to the library search path.
if [[ "$(/opt/amiga/bin/m68k-amigaos-g++ -print-file-name=libdebug.a)" == libdebug.a ]]; then
  mkdir -p "$UE_MINIGL_BUILD_DIR/link-support"
  cp /mnt/d/amiga-gcc2/new/opt/amiga/m68k-amigaos/lib/libdebug.a "$UE_MINIGL_BUILD_DIR/link-support/libdebug.a"
  export UE_MINIGL_EXTRA_LINK_FLAGS="-L$UE_MINIGL_BUILD_DIR/link-support"
fi
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-gcc65-stormflags.sh
