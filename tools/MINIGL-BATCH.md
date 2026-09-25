# Single-TMU BSP batching experiment

Executable: `Unreal-MGL-gcc65-batch`.

- `-mglbatch=0`: original fans, with counters for comparison.
- `-mglbatch=1`: bounded GL_TRIANGLES batches within each surface pass.
- No parameter: original fans, counters disabled. Batching is not a new default.

Only DrawComplexSurfaceSingleTex is changed: base texture, lightmap, detail and
fog each submit a separate batch sequence. A batch never crosses a pass, facet,
texture/blend/depth change, scene node, or frame. No sorting; triangle order,
fan winding, vertex positions and UVs are retained. Models, HUD and multitexture
are unchanged. This is immediate-mode batching, not vertex arrays.

Each glBegin/glEnd contains at most 384 vertices, a whole number of triangles,
below the existing MiniGL vertex-buffer request of 4096. Fan triangulation
duplicates vertices, so fewer submissions do not guarantee higher FPS.
There is no per-frame log output, glFinish, readback or extra synchronization.

At normal renderer shutdown Unreal.log gets `MiniGL batch summary` with:

- frames: Unlock calls since renderer initialization;
- passes: single-TMU surface passes;
- fans: original polygon count across those passes;
- calls: glBegin/glEnd pairs submitted by this path only;
- inputverts / submittedverts: fan vertices versus actually submitted vertices.

Counters reset on renderer initialization (including mode changes); compare
counts per frame if timedemo renders different frame counts. Backend-internal
driver submissions are not measured. Emergency termination may skip the summary.
Do not combine batch=1 with mgltest or mglperf; these combinations are rejected.

Compare the same timedemo, resolution, BPP, lighting and filtering with explicit
`-mglbatch=0` then `-mglbatch=1`. Check both average FPS and image correctness
(masked surfaces, lightmaps, fog, translucent surfaces). The old renderer is
still available via mode 0 and the previously deployed executable is preserved.

Build: `bash tools/build-minigl-gcc65-batch.sh`, new /opt/amiga GCC 6.5, same
optimization flags, -j16, package read cache and texture hash cache retained.
The script supplies missing libdebug.a from the local SDK in an isolated link
directory, just as the previous new-GCC rebuild did. No /opt changes.

Host test `bash tools/tests/test-surface-batch.sh` checks exact ordered triangle
attributes, empty/degenerate input, both modes, 384-vertex boundaries, oversized
fans, randomized inputs, pass isolation, and counters under ASan/UBSan.
Actual MiniGL/PiStorm output and performance require hardware verification.

## 2026-09-21 load failure fix

The initial new-GCC batch EXE (3,456,004 bytes, SHA256 starting 14e96ad0)
failed HUNK segment validation. New runtime archives contain `.debug_loc` and
`.debug_ranges`; the custom linker script left these as orphan output sections.
They were emitted as HUNK_DEBUG followed by relocations without a loadable
segment, with five segments declared in the header. The earlier 2026-09-19
new-GCC BPP executable has the same structural defect (not redeployed here).

The linker script now gathers these and remaining `.debug_*` input sections
into its debug output section. LINK_DEPENDS now makes Ninja relink when the
script changes. Relinking the batch EXE produces a valid three-segment LoadSeg
file. CODE/DATA bytes and BSS size were verified unchanged against the failed
binary. This was host-side file validation, not a WinUAE/PiStorm runtime test.
