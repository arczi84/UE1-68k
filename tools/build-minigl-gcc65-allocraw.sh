#!/usr/bin/env bash
set -euo pipefail
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc65-minigl-o2-allocraw
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-gcc65-allocdiag.sh
