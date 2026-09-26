# MiniGL branch: hardware-tested baseline

This branch preserves the MiniGL port separately from `amiga-port`
(StormMesa/software). On 2026-09-25 the user confirmed the final v27 client
build working on PiStorm and reported 97.52 FPS. This is a user-reported
measurement, not an independently controlled benchmark.

Tested executable SHA256:
`94a32811e668d8a67d7f6575367b58306b48b1034b9295e796ec31eac505a3b5`

## Build and package

Use **GCC 6.5.0b 250215183648**, not the newer GCC 6.5 build currently
installed at `/opt/amiga`. The preserved compiler currently lives under the
misleading `/opt/amiga4.3.2` name; the script prefers
`/opt/amiga-gcc65-250215183648` after that directory is renamed.

```sh
bash tools/build-minigl-gcc65-known-release.sh
bash tools/package-minigl-gcc65-release.sh NEW_PACKAGE_DIRECTORY
```

The build uses `-j16`, validates the compiler banner, and uses that compiler's
native C++ runtime. Required external SDKs are selected by the scripts:
Amiga SDL/NDK under `/mnt/d/amiga-gcc2` and MiniGL Classic v27 under
`/mnt/d/dev/Pistorm3D/MiniGL_Classic_v27`. Adjust paths for another machine.
Neither proprietary game data nor SDK/toolchain binaries are in this repo.

Output: `build/gcc65-known-release-v27/Unreal/Unreal`.
The v27 import archive is rebuilt locally. Its client validates ABI 3 and
the 624-byte prefix actually used by UE, not the unused optional v27
context-from-window/bitmap suffix. No v12 import archive is used.

The release keeps read/texture caches, disables batching and profiling,
and does not create `unreal-first.log`, `startup-debug.log` or `exc-test.log`.
Normal `Unreal.log` and error checks remain. The packaged full INI disables
autotimedemo. Original Unreal retail v200 data is required; upstream also
lists demo v205, which has not been confirmed for this release here.

Other scripts under `tools/` preserve earlier experiments; they are NOT the
current release build entry point. Build products, backups and packages are
intentionally excluded from Git.

## PiStorm3D fullscreen fix (2026-09-25)

PiStorm3D minigl.library 27.6 (V3D backend) keeps the screen bitmap locked
between mglSwitchDisplay and the next frame's first draw in fullscreen, so
CPU and GPU overlap. Intuition calls on the native window in that window
(SetPointer/ClearPointer on mouse capture, SetWindowTitles) froze UE on its
first fullscreen frame. `AmigaMiniGLWindow.c` now calls glFinish (releases
that lock) before those calls and before closing the context.

User-reported on PiStorm: fullscreen now works, 32 FPS vs 25 FPS windowed
(same scene; windowed mode waits for every render and ClipBlits it).
Test build: `tools/build-minigl-gcc65-v27-fslock.sh` -> `Unreal-MGL-v27-fslock`.
The known-release script picks up the source fix on its next rebuild; the
packaged INI still has `StartupFullscreen=False`.
