# Native MiniGL color-depth parameter

`Unreal-MGL-gcc65-bpp -mglbpp=16` requests 16-bit color.
`Unreal-MGL-gcc65-bpp -mglbpp=32` requests 32-bit color.
Omitting the parameter keeps the old 16-bit request. Other values are rejected.
This is color depth, not Z-buffer precision or texture format.

The choice is passed to `mglChoosePixelDepth` before context creation, both at
startup and when recreating the context for resolution/fullscreen changes.
Unreal.log records the requested depth; it is not a measurement of the actual
framebuffer format. The installed minigl.library/backend decides the resulting
format. In a public-screen window, this does not change Workbench's depth.
Use fullscreen for testing dedicated screen depths. Backend support for the
requested format must be checked on the user's PiStorm library version.

Build: `bash tools/build-minigl-gcc65-bpp.sh` (GCC 6.5, `-j16`).
Includes the validated package read cache and texture hash cache. No compiler
optimization, SDL input/audio, lighting or filtering changes.
Deploy `build/gcc65-minigl-o3-bpp/Unreal/Unreal` as `Unreal-MGL-gcc65-bpp`
to the game System directory and FTP `_Pistorm`. Existing known-good EXEs stay.
