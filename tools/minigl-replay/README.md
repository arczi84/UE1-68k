# UE1 actual MiniGL call capture and replay

This is not another generated scene. The capture build replaces only the
process-local MiniGL dispatch pointer with forwarding wrappers. They record
37 immediate-mode/state/texture/presentation calls used by this UE renderer,
copying vector, matrix and pixel payloads before UE can overwrite their memory.
Generated texture names are recorded after glGenTextures and remapped on replay.
No installed graphics library is modified.

## Run

1. Put `Unreal-MGL-capture` beside the game's usual executable in System.
   Keep the usual INI/game arguments and the diagnostic mode that exhibits the
   failure; do not use mode 26 if investigating enabled blending.
2. Run it on WinUAE (known to render correctly), or PiStorm if it can complete
   the recording. It automatically captures from context initialization through
   up to 64 completed swaps. Wait for `MGL capture saved` before copying the file.
3. It creates `PROGDIR:ue1-mgl.cap`. Existing captures are never overwritten:
   rename the previous file before another recording. `FAILED/incomplete` is
   not a usable capture. Do not trigger screenshots during recording.
4. Copy the capture and `MGL-replay` to PiStorm and run:

```
MGL-replay ue1-mgl.cap
```

The replay requires MiniGL but not UE assets, SDL, or audio. It uses captured
dimensions/window mode, the same 16-bit/2-buffer/4096-vertex context choices as
UE, and the recorded lock/sync settings. It preloads the trace to avoid disk
reads between GL calls. At the end it deliberately holds the final image with
`UE replay complete` in the title: this is the end of the recording, not a hang.
Escape/close exits. During replay the title counts completed swaps.

Please preserve the capture: the same file can be compared on both machines
and later reduced without repeatedly rerunning the full game.

## Boundaries

Capture needs an 8 MiB staging buffer. Records are flushed to disk after each
swap, changing CPU timing and potentially masking the original problem.
Capture stops at 64 frames or near the 128 MiB file limit at a frame boundary.
A single frame/initialization exceeding 8 MiB invalidates the capture instead
of silently dropping calls. Replay needs RAM for the whole file plus textures.

Version 1 is native Amiga endian/type layout, not a portable GL interchange
format. It records the current renderer's immediate-mode path, not arbitrary
MiniGL applications, client arrays, application timing, queries, or audio/SDL
interactions. Palette uploads and readbacks explicitly invalidate a capture.
Replay does not regenerate the backend-dependent choices made by UE: it plays
the choices actually recorded. Source/replay backend flags are printed.
Neither host tests nor compilation establish hardware reproduction.

## Build / validation

`make -j16` builds the standalone replay with GCC16 RC9 + modefix, 68060/FPU,
and the v12 dispatch SDK. `make test -j16` runs an ASan/UBSan host mock test:
call counts, scalar/vector/pixel roundtrip, copied payload ownership,
texture-name remapping, RGB record alignment and rejection of truncated input.
This mock does not emulate AmigaOS or MiniGL rendering.

The optional UE CMake setting `UE_MGL_CAPTURE=ON` compiles capture into NSDLDrv.
Default OFF leaves normal builds unchanged. The diagnostic artifact is deployed
as `Unreal-MGL-capture`, never over `Unreal-MGL-diag` or the working game binary.
