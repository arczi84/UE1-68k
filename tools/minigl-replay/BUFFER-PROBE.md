# In-engine buffer probe

Artifact: `Unreal-MGL-buffer`. This is full UE1 with SDL/audio retained, not
the standalone replay. Normal rendering/blend is unchanged; capture is OFF.
Without MGLBUFFER the new probe performs no reads or title updates.

Run separately with the same INI/map/window configuration on PiStorm:

```
Unreal-MGL-buffer -mgltest=0 -mglbuffer=1
Unreal-MGL-buffer -mgltest=0 -mglbuffer=2
Unreal-MGL-buffer -mgltest=0 -mglbuffer=3
```

1. Only swap-attempt/return counters and periodic window titles. No pixel reads
   and no extra GL calls from the probe.
2. Adds CyberGraphX ReadRGBPixel on the native window's central 8x8 area after
   presentation. Does not call glReadPixels or lock the MiniGL back buffer.
3. Also reads the matching back-buffer area via glReadPixels before swap;
   restores GL_PACK_ALIGNMENT afterwards. This can synchronize rendering,
   change hardware lock state and hide the problem. That difference is the
   reason for keeping the first two modes.
4. Optional later `-mglbuffer=4`: adds mglLockBack metadata (address/pitch),
   followed by explicit unlock. It changes locking deliberately and is NOT
   a passive observation or baseline. No private context structures are read.

Sampling/title updates happen every 16 swap attempts. Fields:

- `a`: attempted swaps; `s`: returned swaps.
- `B=changes/valid`: changes of back-buffer sample hash, and validity (0/1).
- `F=changes/valid`: same for native window sample.
- `eq`: equality of the two RGB sample hashes, -1 when unavailable.
- `E`: GL error consumed before the read, then error after read/setup.
- phase: before-swap, read-back, read-window, returned (lock-back in mode 4).

A valid first sample has zero changes. A stationary 8x8 region does not prove
the whole buffer stopped. Leave the game window unobscured and the camera
moving. Pixel-format conversion can cause unequal hashes even for the same
image. CGX reads can also affect timing; neither mode 2 nor 3 is non-invasive.
GL reads that report errors or leave the sentinel untouched are invalid; a
genuine uniformly RGB 165 region is conservatively invalid too.

If B changes but F stays fixed while animation should move, investigate
presentation; if both remain fixed, investigate rendering/upstream state.
Neither alone establishes the cause. If enabling reads makes it work, that
implicates synchronization/timing but is not a proved fix.

Available source inspected: PiStorm3D_V18_Classic-fix/mglQ3 context.c and
others.c. It shows window presentation using ClipBlit and readback using
W3D_WaitIdle plus unlock/relock. This is NOT verified to be the exact installed
Amiga library build. The probe uses public v12 SDK calls instead of assuming
its internal struct layout. The installed library is not patched/replaced.

Build uses GCC16 RC9 + modefix/FPU, target Unreal with -j16. Amiga build and
diff checks pass; the behavior still needs the hardware comparison.

## Classic-capture blend factors A/B

The previously inspected WinUAE capture reports backend=2 (Classic), while
the user's grey-window probe screenshot reports backend=4 (PiStorm).
SetBlend chooses different factors for these backends; replay preserves the
captured factors rather than repeating UE's selection on PiStorm.

`-mglblendcompat=3` forces ONLY these two Classic blend-factor choices in full UE:

- Translucent: ONE/ONE instead of ONE/ONE_MINUS_SRC_COLOR.
- Modulated: DST_COLOR/ZERO instead of DST_COLOR/SRC_COLOR.

Blend stays enabled. Backend flags, locking, textures, SDL and audio are not
changed. Default 0 preserves the original choices. Value 1 changes only
translucent, 2 only modulated, 3 both. The Amiga lightmap pass already has its
own DST_COLOR/ZERO override and remains unchanged.

```
Unreal-MGL-buffer -mgltest=0 -mglbuffer=1 -mglblendcompat=3
```

Compare against the same command with `-mglblendcompat=0`. Mode 1 avoids the
pixel-read synchronization of mode 3. This is an unconfirmed hypothesis test,
not a claim that either equation is globally unsupported on PiStorm.

## Selectively enable blend with MGLBLEND

`-mglblend=N` selects a bit mask without changing blend factors, textures,
alpha testing, depth masks, geometry, or SDL/audio. Omit it for unchanged
behavior. `-mgltest=26` takes precedence and disables all blend regardless of
this mask: use `-mgltest=0` when enabling selected groups.

| Value | Enabled groups |
|---|---|
| 0 | None (baseline equivalent to disabling blend in test 26) |
| 1 | PF_Translucent surfaces/effects |
| 2 | PF_Modulated surfaces, including lightmap/detail passes |
| 4 | PF_Highlighted, including fog/flash/highlighted overlays |
| 7 | All three above, but initial/copy blend remains disabled |
| 8 | Initial/copy blend only (separate diagnostic group) |
| 15 | All groups, including initial/copy blend |

Bits can be added (e.g. 3 enables translucent + modulated). Classification
uses the same precedence as SetBlend: translucent, modulated, highlighted.
The explicit lightmap enable is classified as modulated too. No effect's
draw calls are removed: deselected groups are drawn with blending disabled,
which intentionally produces wrong lighting/transparency during isolation.
Groups absent from the current scene are not exercised by that run.

```
Unreal-MGL-buffer -mgltest=0 -mglbuffer=1 -mglblend=1
Unreal-MGL-buffer -mgltest=0 -mglbuffer=1 -mglblend=2
Unreal-MGL-buffer -mgltest=0 -mglbuffer=1 -mglblend=4
```

Keep MGLBLENDCOMPAT omitted/0 for the first comparison. The mask is read at
startup, not interactively toggled during rendering.
