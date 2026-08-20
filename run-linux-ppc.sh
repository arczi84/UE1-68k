#!/usr/bin/env bash
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
SYSTEM_DIR="$SCRIPT_DIR/Game/UnrealLinuxTest/System"

cd "$SYSTEM_DIR"
export QEMU_LD_PREFIX="/usr/powerpc-linux-gnu"
export LD_LIBRARY_PATH="."
exec qemu-ppc -cpu G4 ./Unreal_ppc -nosound "$@"
