# RC11 stable-path rebuild (2026-09-25)

Build script: `tools/build-minigl-rc11-stable.sh`, fresh directory
`build/gcc16-rc11-stable`, `-j16`.
Compiler: `/opt/amiga-gcc16-rc11`, GCC 16.2.0b 20260825082934.
Output/deployment name: `Unreal-MGL-rc11`.

This rebuild uses the current tree's known-working renderer path and the
validated texture hash cache / 64 KiB package read cache. It is not a checkout
of an exact historic source snapshot. `UE_MINIGL_STABLE_BASELINE` compiles out
the batch submission branch and disables its counters. Array/allocation
diagnostics, capture and profiling are not enabled. Existing BPP selection
remains available, with the original 16-bit request as default. Linker HUNK
debug-section fixes are retained.

CPU/optimization: 68060, hard float, omit frame pointer, fast math, effective O3,
no strict aliasing from the existing build configuration; existing UnCorSc /
UnProp per-file Og settings unchanged. CRT nix20. No RC9 -B override, no
alignment-ABI changes. Runtime and libstdc++ paths in the map resolve to RC11.

Validation completed: HUNK parser accepts three loadable segments; isolated
Amiga/m68k ConsoleBack mipmap reader compiled with RC11 passes in vamos (all nine
mips, end position 149153); host read-cache, menu and BPP tests pass. No batching
helper or allocation diagnostic symbols in the map. The pre-existing UProperty
duplicate-section linker warning remains. Full game startup/rendering on the
user's Amiga still requires testing; the isolated test is not proof of that.

Existing game/FTP executables are preserved; this is a separately named build.
