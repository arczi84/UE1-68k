#!/usr/bin/env bash
set -euo pipefail
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc65-minigl-o3-release
# Stable baseline: validated texture hash cache plus the package read cache
# enabled by the shared build script. No allocation diagnostics or profiling.
export UE_MINIGL_EXTRA_FLAGS=-DUE_MINIGL_HASHCACHE
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-gcc65-stormflags.sh
