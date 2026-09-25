#!/usr/bin/env bash
set -euo pipefail
ue_testdir=$(mktemp -d /tmp/ue1-texture-cache-test.XXXXXX)
g++ -std=c++14 -O2 -Wall -Wextra -fsanitize=address,undefined \
  /mnt/d/dev/UE1-MiniGL/tools/tests/test-texture-bind-cache.cpp -o "$ue_testdir/check"
"$ue_testdir/check"
