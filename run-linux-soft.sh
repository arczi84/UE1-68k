#!/bin/bash
# Uruchamia build software'owy na Linuksie (x86 32-bit) przez WSLg.
cd /mnt/d/dev/UE1/Game/UnrealLinuxSoft/System
DISPLAY=:0 LD_LIBRARY_PATH=. ./Unreal.bin "$@"
