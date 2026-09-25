#!/usr/bin/env bash
set -euo pipefail
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc65-minigl-o3-filtercache
export UE_MINIGL_EXTRA_FLAGS='-DUE_MINIGL_HASHCACHE -DUE_MINIGL_FILTERCACHE'
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-gcc65-stormflags.sh
