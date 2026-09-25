#!/usr/bin/env bash
set -euo pipefail
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc65-minigl-o3-menufix
export UE_MINIGL_EXTRA_FLAGS=-DUE_ALLOC_DIAG
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-gcc65-stormflags.sh
