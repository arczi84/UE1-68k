# GCC 6.5 package read cache (stable default)

The user confirmed faster loading on PiStorm. The shared GCC 6.5 build script
now enables this cache by default, including the standard release build.
The release also retains the measured texture hash-cache improvement.
Existing deployed executables are not replaced by this build-script change;
the tested `Unreal-MGL-gcc65-readcache` already has the same defaults.

Build with `bash tools/build-minigl-gcc65-readcache.sh` (`-j16`).
Output: `build/gcc65-minigl-o3-readcache/Unreal/Unreal`.
Deploy name: `Unreal-MGL-gcc65-readcache` in the game System folder and FTP `_Pistorm`.

Uses the GCC 6.5 StormMesa-matching flags and the measured texture hash-cache
improvement. Does not enable the separate filter-cache experiment, profiling,
or allocation diagnostics. Rendering and game configuration are unchanged.

- `-mglreadcache=1`: enabled (default).
- `-mglreadcache=0`: original stdio read/seek path, in the same executable.

Each package loader allocates 64 KiB lazily on its first small cache miss and
frees it on destruction. Small reads and seeks within the cached range avoid
stdio calls. Large reads bypass the buffer. Tell and nested Push/Pop use logical
positions; the physical FILE cursor is synchronized only when necessary.
No whole-package preload and no changes to byte-order conversion.

Compare startup to the first visible flyby frame and loading the same map with
the same settings. Record first-run and repeated-run times separately: filesystem
caching can otherwise obscure the difference. This improves loading, not a
promised timedemo FPS improvement.

`bash tools/tests/test-readcache.sh` tests the actual loader class with host stdio
and ASan/UBSan, both with and without the compile-time feature. Covers both runtime
modes, sequential and randomized reads/seeks, large reads, nested Push/Pop,
multiple loaders, EOF, empty files, failed seeks and reads. The user has confirmed
faster loading on PiStorm; no quantitative loading-time measurement was supplied.
