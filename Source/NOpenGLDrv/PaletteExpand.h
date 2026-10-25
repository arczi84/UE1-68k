#pragma once

// Native big-endian upload words. Rebuild per upload: procedural palettes can
// change without changing their address. Preserve RGB for masked index zero.
template<class Color>
static void ExpandPaletteRGBA(const BYTE* Src, DWORD* Dst, DWORD Count,
                              const Color* Palette, UBOOL Masked)
{
    DWORD Table[256];
    if (Count < 256)
    {
        for (DWORD i=0; i<Count; ++i)
        {
            const BYTE Index=Src[i];
            const Color& C=Palette[Index];
            Dst[i]=(DWORD(C.R)<<24)|(DWORD(C.G)<<16)|(DWORD(C.B)<<8)
                | ((Masked && !Index) ? 0u : 255u);
        }
        return;
    }
    for (DWORD i=0; i<256; ++i)
    {
        const Color& C=Palette[i];
        Table[i]=(DWORD(C.R)<<24)|(DWORD(C.G)<<16)|(DWORD(C.B)<<8)|255u;
    }
    if (Masked) Table[0] &= 0xffffff00u;
    for (DWORD i=0; i<Count; ++i) Dst[i]=Table[Src[i]];
}
