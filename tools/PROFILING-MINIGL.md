# MiniGL profiling

## Current GCC 6.5 release comparison

Build: `bash tools/build-minigl-gcc65-pg.sh` (`-j16`).
Output: `build/gcc65-minigl-o3-pg/Unreal/Unreal`.
Deployment: `Unreal-MGL-gcc65-pg`.

Matches the current GCC 6.5 O3/68060/hard-float/fast-math release, including
weapon-layout and filter-menu fixes. Render-statistic timers stay disabled;
allocation diagnostics stay disabled. Adds `-pg -DUE_AMIGA_GPROF` and replaces
`-fomit-frame-pointer` with `-fno-omit-frame-pointer`. The two Core `-Og`
exceptions and the uninstrumented sampler's O2 override remain unchanged.

Run the same timedemo on PiStorm with the same INI and renderer arguments.
Let autotimedemo finish and exit normally, then collect `gmon.out`,
`gprof-sampler.log`, `gprof-histogram.bin` and `Unreal.log` from System.
Archive them before running again. These are profiling results, not release FPS.

Analyze with the matching binary (never the old GCC16 executable):

```sh
/opt/amiga/bin/m68k-amigaos-gprof -b \
  /mnt/d/dev/UE1-MiniGL/build/gcc65-minigl-o3-pg/Unreal/Unreal \
  /path/to/gmon.out
```

## Older GCC16 RC9, 68060 build

Build with `bash tools/build-minigl-gprof-060.sh` (`-j16`).
Output: `build/gcc16-rc9-modefix-minigl-gprof-060/Unreal/Unreal`.
Deployment name: `Unreal-MGL-pg-060`.

Uses the O3 configuration, `-mcpu=68060 -m68881 -fno-fast-math
-fno-strict-aliasing`, with the existing two UnrealScript `-Og` exceptions.
`-pg` requires `-fno-omit-frame-pointer`. The sampler itself is not instrumented
and retains its existing O2 pragma. Symbols are retained; debug information is
stripped because the tools reject the unstripped output's debug hunks.

Run with the same INI, resolution and renderer arguments as the normal build.
Autotimedemo saves profiling data before writing FPS and tearing down graphics.
F10 and normal exit also save before renderer teardown. Cleanup is idempotent.
No normal-build profiling code is enabled without `UE_AMIGA_GPROF`.

Collect these files beside the executable after the run:

- `gmon.out`
- `gprof-sampler.log`
- `gprof-histogram.bin`

These filenames are reused on each run; archive a result before another run.
Keep the exact matching executable and map from the build directory.
Analyze with:

```sh
/opt/amiga-gcc16-rc9/bin/m68k-amigaos-gprof -b \
  /mnt/d/dev/UE1-MiniGL/build/gcc16-rc9-modefix-minigl-gprof-060/Unreal/Unreal \
  /path/to/gmon.out
```

Profiling adds overhead: do not compare its FPS directly with release FPS.
Collection includes startup/loading. The existing sampler hooks the Level 3
autovector and records interrupted PCs inside executable text. `foreign` counts
PCs outside that text, including shared libraries, OS and other tasks; it cannot
be interpreted as MiniGL time alone. Shared MiniGL functions are not individually
instrumented. Inspect sample totals before drawing conclusions from percentages.
The legacy gmon format does not encode the sampling frequency; verify the actual
VBlank rate before interpreting gprof's absolute seconds.

Build and static checks completed; hardware collection requires a user run.
