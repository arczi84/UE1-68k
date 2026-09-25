#!/usr/bin/env bash
set -euo pipefail
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc16-rc9-modefix-minigl-classic-perf
export UE_MINIGL_EXTRA_FLAGS=-DUE_DISABLE_RENDER_TIMING
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-o3-060.sh
