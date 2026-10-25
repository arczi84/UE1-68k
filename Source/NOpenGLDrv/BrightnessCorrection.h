#pragma once
#include <math.h>

// Fixed-function MiniGL has no window gamma ramp. Correct RGB at upload time;
// alpha stays unchanged so masked sprites, fonts and translucent edges survive.
inline void BuildMiniGLBrightnessTable(unsigned char* Table, float Brightness)
{
    const double Gamma = 0.5 + 1.5 * (Brightness < 0 ? 0 : (Brightness > 1 ? 1 : Brightness));
    for (int I = 0; I < 256; ++I)
        Table[I] = (unsigned char)(255.0 * pow(I / 255.0, 1.0 / Gamma) + 0.5);
}
// SoftDrv brightness model: lighting is scaled linearly by 0.5+Brightness
// (1.0 at the default 0.5). Unlike a gamma curve this keeps light hues and
// does not lift dim coloured ambient light into a tint over the whole scene.
inline void BuildMiniGLLightScaleTable(unsigned char* Table, float Brightness)
{
    const double Scale = 0.5 + (Brightness < 0 ? 0 : (Brightness > 1 ? 1 : Brightness));
    for (int I = 0; I < 256; ++I)
    {
        const double V = I * Scale + 0.5;
        Table[I] = (unsigned char)(V > 255.0 ? 255.0 : V);
    }
}
inline void ApplyMiniGLBrightness(unsigned char* Pixels, unsigned int Count, const unsigned char* Table)
{
    for (unsigned int I = 0; I < Count; ++I, Pixels += 4)
        for (int C = 0; C < 3; ++C)
            Pixels[C] = Table[Pixels[C]];
}

// Classic supports 1x modulation. Bake the 2x detail factor into RGB;
// 128 becomes white (neutral) and darker values retain detail contrast.
// Values above neutral saturate because this backend cannot multiply >1.
inline void ExpandMiniGLDetail(unsigned char* Pixels, unsigned int Count)
{
    for (unsigned int I = 0; I < Count; ++I, Pixels += 4)
        for (int C = 0; C < 3; ++C)
            Pixels[C] = Pixels[C] < 128 ? Pixels[C] * 2 : 255;
}
