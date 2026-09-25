#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_testdir=$(mktemp -d /tmp/ue1-allocraw-test.XXXXXX)
{
  printf '%s\n' '#include <cassert>' \
    'struct Node { Node* ln_Succ; };' \
    'struct MemHeader { Node mh_Node; void* mh_Lower; void* mh_Upper; };' \
    'struct ExecBase { struct { Node* lh_Head; } MemList; };' \
    'ExecBase base; ExecBase* SysBase=&base;' \
    'int depth=0; void Forbid(){++depth;} void Permit(){--depth;}'
  sed -n '/^int AmigaAllocReadable(/,/^}/p' "$ue_root/Source/Unreal/Src/AmigaStack.c"
  printf '%s\n' 'int main() {' \
    'Node tail={0}; MemHeader ram={{&tail},(void*)0x1000,(void*)0x2000};' \
    'base.MemList.lh_Head=&ram.mh_Node;' \
    'assert(AmigaAllocReadable((void*)0x1000,4096));' \
    'assert(AmigaAllocReadable((void*)0x1fff,1));' \
    'assert(!AmigaAllocReadable((void*)0x1fff,2));' \
    'assert(!AmigaAllocReadable((void*)0xfff,2));' \
    'assert(!AmigaAllocReadable((void*)0xfffffff0UL,32));' \
    'assert(!AmigaAllocReadable(0,1));' \
    'assert(!AmigaAllocReadable((void*)0x1000,0));' \
    'assert(!AmigaAllocReadable((void*)0x1000,4097));' \
    'assert(depth==0);' '}'
} | g++ -x c++ -O2 -Wall -Wextra -o "$ue_testdir/check" -
"$ue_testdir/check"
printf '%s\n' 'RAM range boundary checks passed (mock Exec memory list).'
