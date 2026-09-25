# MiniGL blend/display probe

Standalone AmigaOS test derived from UE1's observed PiStorm symptoms. This is
a candidate reproducer, not yet confirmed to reproduce the bug on hardware.
No UE1 data, SDL, audio, lightmaps or engine code is required. Uses a 640x480
window, 16-bit buffer request, 4096-vertex MiniGL buffer and generated RGBA
textures. A moving card and scrolling background must animate continuously.

Build with `make -j16` in this directory. Uses GCC16 RC9 with the modefix
compiler override and the same v12 dispatch SDK as UE1. Output goes into
`../../build/minigl-blend-test/MGL-blend-test`.

After restarting PiStorm, run these separately (restart again after a failed
run; retained state may affect subsequent results):

```
MGL-blend-test -mode=off
MGL-blend-test -mode=copy
MGL-blend-test -mode=copy -tri
```

`off` disables blending; `copy` enables ONE/ZERO. Geometry and animation are
otherwise identical. `-tri` emits each triangle separately, while the default
uses a four-vertex fan. This compares the conditions of UE1 tests 14/25/27.

Other modes: `alpha` = SRC_ALPHA/ONE_MINUS_SRC_ALPHA, `add` = ONE/ONE,
`mod` = DST_COLOR/ZERO, `ue` = opaque background followed by modulated,
additive and alpha-tested surfaces. The latter exercises state transitions;
it does not implement UE1 lighting.

## UE state-sequence isolation (revision 2)

The original modes are preserved. `-stage=N` selects a separate synthetic
scene using UE1 SetBlend's precedence and cached state-delta logic, including
the current Classic-backend blend substitutions. It is not a captured UE
frame and does not reproduce the engine texture cache or scene traversal.

Run stages separately, stopping at the first failure:

```
MGL-blend-test -stage=1
MGL-blend-test -stage=2
MGL-blend-test -stage=3
MGL-blend-test -stage=4
```

1. Repeated opaque/masked surfaces, translucent sprites, highlighted quads;
   blend/alpha/depth-write changes are cached across calls and frames.
2. Adds a same-geometry modulated second pass using UE's Amiga lightmap blend
   override and GL_EQUAL for masked surfaces (generated checker, not lighting).
3. Adds the highlighted fog-style same-geometry pass.
4. Adds glTexImage2D reuploads to active texture objects between draws.

Default is 32 surface groups. `-count=256` increases the number of transitions
and submitted vertices. `-tri`, `-lock=default`, `-depthonly`, and `-readback`
remain available for one-variable comparisons of a failing stage. Space is
disabled for staged runs; Tab still switches primitives. Stage and count are
shown in the window title. Hardware reproduction remains unconfirmed.

## Polygon/clipping isolation (revision 3)

User reports all stages 1–4 working on PiStorm: no reproducer found yet.
Stages 5–6 branch from stage 3 (no repeated texture uploads):

```
MGL-blend-test -stage=5
MGL-blend-test -stage=6
```

5. Replaces the surface quads with convex planar polygons of 3–16 vertices;
   keeps the blend transitions and same-depth passes of stage 3.
6. Enlarges and tilts these polygons to cross the side, near and eye planes,
   exercising MiniGL clipping. This is a synthetic stress case, not a claim
   that UE submits exactly these polygons (UE also performs its own clipping).

For a failing stage, repeat with `-tri` to submit the identical geometry as
separate triangles. Old modes/stages and the UE executable are unchanged.

## Texture-cache isolation (revision 4)

User reports stages 5–6 working too. Stages 7–8 branch from stage 3's quads
and passes, not from clipping stage 6:

```
MGL-blend-test -stage=7
MGL-blend-test -stage=8
```

7. Lazily creates 24 textures with power-of-two dimensions from 32 to 256,
   including rectangular images. All uploads reuse one heap buffer filled
   with different RGBA data. Initial uploads occur between draws, followed
   by alternating NEAREST/LINEAR filters; filters are restored on cache hits.
   One texture per frame is refreshed, with repeated uses sharing that upload.
8. Same draws and uploads, but reallocates the shared buffer to each uploaded
   image's size. This may move/free old storage; relocation is not guaranteed.
   This is a lifetime stress test, not UE's exact growth-only allocation rule.

These stages ignore `-nearest` and choose filters per texture. Both use the
same generated pixel data. They do not replay UE assets or captured GL calls.
Stages 1–6 are unchanged. Hardware reproduction is still unconfirmed.

Space cycles modes; Tab switches fan/triangles; Escape or close exits.
Switching in-process is convenient but cannot establish a clean-state result.

The title shows frame and completed swap counters and the latest GL error.
`returned` means mglSwitchDisplay returned, not proof that a new image appeared.
No console debug spam or log files. Title updates occur every 16 frames and
can affect scheduling. All modes request VSync off.

Optional `-readback` hashes a 4x4 RGB sample in the back buffer before each swap.
`changes` counts sample changes; a constant sample alone does not prove a
stalled buffer. A GL error invalidates the readback inference. Readback can
introduce synchronization and hide timing bugs, so try it only after a failure
without it. It does not read the displayed window/front buffer.

`-lock=smart` is the default, matching UE1. `-lock=default` leaves MiniGL's
context default unchanged, as dethrace does. `-lock=manual` explicitly locks
before rendering and unlocks before presentation; `-lock=auto` requests
per-primitive locking. `-nearest` changes filtering; `-depthonly` clears color
only on the first frame, closer to UE1's usual depth-only clear.

Report startup command, platform/library version, whether animation moves,
frame/swap counters, GL error, and (if enabled) readback changes. Compare the
same executable and settings on WinUAE and PiStorm. No libraries are replaced.
