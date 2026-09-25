#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_testdir=$(mktemp -d /tmp/ue1-filter-test.XXXXXX)
{
  printf '%s\n' '#include <cassert>' '#define NOPENGLDRV_USE_MINIGL' \
    'typedef int UBOOL; typedef unsigned int DWORD; typedef unsigned int GLenum;' \
    'enum { PF_NoSmooth=1,GL_TEXTURE_2D=2,GL_TEXTURE_MIN_FILTER=3,GL_TEXTURE_MAG_FILTER=4,GL_NEAREST=5,GL_LINEAR=6 };' \
    'int calls=0; GLenum minf=0,magf=0; int GAmigaPerfMask=0,GAmigaFilterCache=0;' \
    'void glTexParameteri(GLenum,GLenum p,GLenum v) { ++calls; if(p==GL_TEXTURE_MIN_FILTER) minf=v; else magf=v; }' \
    'struct FTextureInfo { void* Palette; };' \
    'class UNOpenGLRenderDevice { public: unsigned char TextureFilter=1; bool NoFiltering=true;' \
    'struct FCachedTexture { GLenum AppliedMinFilter=0,AppliedMagFilter=0; };' \
    'UBOOL WantsNearest(const FTextureInfo&,DWORD) const;' \
    'void SetCachedTextureFilter(FCachedTexture*,GLenum,GLenum); };'
  sed -n '/^UBOOL UNOpenGLRenderDevice::WantsNearest(/,/^}/p' "$ue_root/Source/NOpenGLDrv/NOpenGLDrv.cpp"
  sed -n '/^void UNOpenGLRenderDevice::SetCachedTextureFilter(/,/^}/p' "$ue_root/Source/NOpenGLDrv/NOpenGLDrv.cpp"
  printf '%s\n' 'int main() {' \
    'UNOpenGLRenderDevice r; FTextureInfo info={(void*)1};' \
    'assert(!r.WantsNearest(info,0)); assert(r.WantsNearest(info,PF_NoSmooth));' \
    'r.TextureFilter=0; assert(r.WantsNearest(info,0));' \
    'UNOpenGLRenderDevice::FCachedTexture a,b;' \
    'r.SetCachedTextureFilter(&a,GL_NEAREST,GL_NEAREST);' \
    'r.SetCachedTextureFilter(&b,GL_NEAREST,GL_NEAREST);' \
    'r.TextureFilter=1; GLenum f=r.WantsNearest(info,0)?GL_NEAREST:GL_LINEAR;' \
    'r.SetCachedTextureFilter(&a,f,f); assert(minf==GL_LINEAR && magf==GL_LINEAR);' \
    'r.SetCachedTextureFilter(&b,f,f); assert(calls==8);' \
    'GAmigaPerfMask=2; r.SetCachedTextureFilter(&b,f,f); assert(calls==8);' \
    'r.TextureFilter=0; r.SetCachedTextureFilter(&b,GL_NEAREST,GL_NEAREST);' \
    'assert(calls==10 && minf==GL_NEAREST && magf==GL_NEAREST);' \
    'GAmigaPerfMask=0; GAmigaFilterCache=1;' \
    'r.SetCachedTextureFilter(&b,GL_NEAREST,GL_NEAREST); assert(calls==10);' \
    'r.SetCachedTextureFilter(&a,GL_LINEAR,GL_LINEAR); assert(calls==10);' \
    'r.SetCachedTextureFilter(&b,GL_LINEAR,GL_LINEAR); assert(calls==12);' \
    'b.AppliedMinFilter=b.AppliedMagFilter=0; /* upload invalidates cache */' \
    'r.SetCachedTextureFilter(&b,GL_LINEAR,GL_LINEAR); assert(calls==14);' \
    'r.SetCachedTextureFilter(&b,GL_NEAREST,GL_LINEAR); assert(calls==16);' \
    'assert(minf==GL_NEAREST && magf==GL_LINEAR);' \
    'GAmigaFilterCache=0; r.SetCachedTextureFilter(&b,GL_NEAREST,GL_LINEAR); assert(calls==18);' '}'
} | g++ -x c++ -O2 -Wall -Wextra -o "$ue_testdir/check" -
"$ue_testdir/check"
printf '%s\n' 'MiniGL filter selection and cached filter update checks passed.'
