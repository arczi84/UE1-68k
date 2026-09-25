# Classic performance A/B test

Executable: `Unreal-MGL-classic-perf`. Built by
`bash tools/build-minigl-classic-perf.sh`, always `-j16`.
Keeps O3/060/FPU, non-fast-math, no detailed render timers, exit and autotimedemo
cancellation fixes. No -pg or MiniGL library changes.

Run the same full flyby, resolution, audio and INI for each value:

| Argument | Change |
|---|---|
| `-mglperf=0` | Baseline SMART locking and original filter calls |
| `-mglperf=1` | MANUAL lock acquired before rendering, released after flush before swap |
| `-mglperf=2` | Skip identical filter pairs already applied to the same texture |
| `-mglperf=3` | Both changes |

Use these instead of `-mgltest`, not together. Modes 1–3 require Classic and fail
explicitly on another backend. `Unreal.log` records requested mode/backend.
Shaders/effects, geometry, texture format and filter choice are not disabled.
Uploads invalidate the filter cache; changing filters always sets both members
of the pair because Classic also keeps context-wide filter values.

Compare FPS only if the image, filtering, transparency and animation remain
correct. The frame-lock experiment is not the old test 35, which selected
MANUAL without adding explicit frame lock/unlock calls. Failed frame locks
skip rendering, and no-swap frames still release the lock.

Host tests validate helper logic and call ordering, not Amiga driver behavior:
`python3 tools/tests/test_classic_perf.py`.
