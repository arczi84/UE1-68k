#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_diag_testdir=$(mktemp -d /tmp/ue1-allocdiag-test.XXXXXX)
# Compile the actual checked multiplication helper, not a duplicate model.
{
  printf '%s\n' '#include <cstddef>' '#include <cassert>' \
    'typedef int INT;' 'static int reports=0;' \
    'void appAllocDiagFailure(INT,const char*) { ++reports; }' \
    'void appErrorf(const char*,...) { throw 1; }'
  sed -n '/^inline INT AllocDiagBytes(/,/^}/p' "$ue_root/Source/Core/Inc/UnMem.h"
  printf '%s\n' 'int main() {' \
    'assert(AllocDiagBytes(32,100)==3200);' \
    'assert(AllocDiagBytes(32,0)==0);' \
    'assert(AllocDiagBytes(1,2147483647)==2147483647);' \
    'try { AllocDiagBytes(32,-1); assert(false); } catch(int) {}' \
    'try { AllocDiagBytes(32,67108864); assert(false); } catch(int) {}' \
    'assert(AllocDiagBytes(32,67108863)==2147483616);' \
    'assert(reports==2);' '}'
} | g++ -x c++ -O2 -Wall -Wextra -o "$ue_diag_testdir/check" -
"$ue_diag_testdir/check"
printf '%s\n' 'Allocation multiplication boundary checks passed.'
