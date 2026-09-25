#!/usr/bin/env bash
set -euo pipefail
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc65-release-package
export UE_MINIGL_EXTRA_FLAGS='-g0 -DUE_MINIGL_HASHCACHE -DUE_MINIGL_STABLE_BASELINE -DUE_RELEASE_PACKAGE'
if [[ "$(/opt/amiga/bin/m68k-amigaos-g++ -print-file-name=libdebug.a)" == libdebug.a ]]; then
  mkdir -p "$UE_MINIGL_BUILD_DIR/link-support"
  cp /mnt/d/amiga-gcc2/new/opt/amiga/m68k-amigaos/lib/libdebug.a "$UE_MINIGL_BUILD_DIR/link-support/libdebug.a"
  export UE_MINIGL_EXTRA_LINK_FLAGS="-L$UE_MINIGL_BUILD_DIR/link-support"
fi
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-gcc65-stormflags.sh
