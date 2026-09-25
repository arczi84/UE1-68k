#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
export UE_RC11_BUILD_DIR="$ue_root/build/gcc16-rc11-release"
export UE_RC11_EXTRA_FLAGS='-g0 -DUE_RELEASE_PACKAGE'
bash "$ue_root/tools/build-minigl-rc11-stable.sh"
