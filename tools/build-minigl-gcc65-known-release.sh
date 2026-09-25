#!/usr/bin/env bash
set -euo pipefail
# Prefer the descriptive name after the administrator renames the directory.
export UE_MINIGL_TOOLCHAIN_ROOT=/opt/amiga-gcc65-250215183648
if [[ ! -d "$UE_MINIGL_TOOLCHAIN_ROOT" ]]; then
  export UE_MINIGL_TOOLCHAIN_ROOT=/opt/amiga4.3.2
fi
ue_banner=$("$UE_MINIGL_TOOLCHAIN_ROOT/bin/m68k-amigaos-g++" --version)
[[ "$ue_banner" == *'6.5.0b 250215183648'* ]] || { echo 'Wrong compiler; refusing build.' >&2; exit 1; }
export UE_MINIGL_BUILD_DIR=/mnt/d/dev/UE1-MiniGL/build/gcc65-known-release-v27
export UE_MINIGL_SDK_ROOT=/mnt/d/dev/UE1-MiniGL/build/sdk-minigl-v27-gcc65
bash /mnt/d/dev/UE1-MiniGL/tools/prepare-minigl-v27-sdk.sh
export UE_MINIGL_EXTRA_FLAGS='-g0 -DUE_MINIGL_HASHCACHE -DUE_MINIGL_STABLE_BASELINE -DUE_RELEASE_PACKAGE'
exec bash /mnt/d/dev/UE1-MiniGL/tools/build-minigl-gcc65-stormflags.sh
