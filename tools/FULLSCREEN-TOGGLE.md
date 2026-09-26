# Native MiniGL context recreation (2026-09-26)

`RecreateNativeMiniGL` exits and reinitializes the same render device around
context deletion/creation. Its `CurrentSceneNode` cache previously survived
`Init`. At unchanged resolution/FOV, `SetSceneNode` therefore skipped both
viewport and projection setup on the new context. This is a renderer lifecycle
bug, independent of batching.

`Init` now calls `SetSceneNode(NULL)` after assigning `Viewport`. The next draw
restores viewport/projection; subsequent frames retain the normal cache.
Existing PiStorm3D display-lock release before Intuition operations is retained.

Reference inspected: https://github.com/SteffenHaeuser/MiniGL_Library_68k/blob/PiStorm3D/gl/src/context.c

Validation: `python3 tools/tests/test-context-reinit.py` compiles the actual
`SetSceneNode` implementation with mocked GL calls, reproduces the old cache
failure, then tests repeated invalidation and a resolution change. This is not
a hardware presentation test.

Build: `bash tools/build-minigl-gcc65-v27-fslock.sh` (known GCC 6.5, v27 SDK,
release, -j16, no experimental batching). Validated three-segment Amiga HUNK.
Test executable: `D:\dev\UE1\Game\Unreal68k\System\Unreal-MGL-v27-toggle`.
Existing game executables and INI are unchanged.

User reported the toggle build working on 2026-09-26 and requested publication.
Regression check: Alt+Enter from window to fullscreen and back, repeated at the
same resolution, while checking image updates and input.
