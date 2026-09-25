#!/usr/bin/env bash
set -euo pipefail
ue_testdir=$(mktemp -d /tmp/ue1-surface-batch.XXXXXX)
g++ -std=c++14 -O2 -g -Wall -Wextra -fsanitize=address,undefined \
  -fno-omit-frame-pointer -fno-pie -no-pie \
  /mnt/d/dev/UE1-MiniGL/tools/tests/test-surface-batch.cpp -o "$ue_testdir/test"
"$ue_testdir/test"
