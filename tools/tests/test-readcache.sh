#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_testdir=$(mktemp -d /tmp/ue1-readcache.XXXXXX)
for ue_mode in enabled original; do
  ue_define=()
  if [[ "$ue_mode" == enabled ]]; then ue_define=(-DUE_MINIGL_READCACHE); fi
  {
    sed -n '/^class FArchiveFileLoad :/,/^};/p' "$ue_root/Source/Core/Src/UnLinker.h"
    sed -n '1,$p' "$ue_root/tools/tests/test-readcache.cpp"
  } | g++ -x c++ -std=c++14 -O2 -g -Wall -Wextra -Wno-reorder -Wno-unused-parameter \
      -fsanitize=address,undefined -fno-omit-frame-pointer -fno-pie -no-pie \
      "${ue_define[@]}" -include "$ue_root/tools/tests/readcache-mocks.h" \
      -o "$ue_testdir/$ue_mode" -
  "$ue_testdir/$ue_mode"
done
