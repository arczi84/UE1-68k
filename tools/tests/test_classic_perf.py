#!/usr/bin/env python3
"""Compile actual perf helper functions with mock GL calls on the host."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
render = (root / "Source/NOpenGLDrv/NOpenGLDrv.cpp").read_text()
window = (root / "Source/NSDLDrv/Src/AmigaMiniGLWindow.c").read_text()
viewport = (root / "Source/NSDLDrv/Src/NSDLViewport.cpp").read_text()
begin = render.index("void UNOpenGLRenderDevice::SetCachedTextureFilter(")
end = render.index("void UNOpenGLRenderDevice::EnsureComposeSize(", begin)
filters = render[begin:end].replace("UNOpenGLRenderDevice::", "")
begin = window.index("void AmigaMiniGLSetFrameLock(")
end = window.index("void AmigaMiniGLSwapBuffers(", begin)
locking = window[begin:end]
lock = viewport[viewport.index("UBOOL UNSDLViewport::Lock("):viewport.index("void UNSDLViewport::Unlock(")]
unlock = viewport[viewport.index("void UNSDLViewport::Unlock("):]
assert lock.index("AmigaMiniGLBeginFrame()") < lock.index("UViewport::Lock(")
assert "!Locked ) AmigaMiniGLEndFrame()" in lock
assert unlock.index("UViewport::Unlock( Blit )") < unlock.index("AmigaMiniGLEndFrame()") < unlock.index("if( Blit &&")
program = r'''
#include <cassert>
#define NOPENGLDRV_USE_MINIGL
typedef unsigned GLenum;
struct FCachedTexture { GLenum AppliedMinFilter, AppliedMagFilter; };
int GAmigaPerfMask=0, GAmigaFilterCache=0, calls=0;
enum { GL_TEXTURE_2D=1, GL_TEXTURE_MIN_FILTER=2, GL_TEXTURE_MAG_FILTER=3 };
GLenum lastMin=0, lastMag=0;
void glTexParameteri(GLenum, GLenum pname, GLenum value) {
    ++calls;
    if(pname==GL_TEXTURE_MIN_FILTER) lastMin=value; else lastMag=value;
}
enum { MGL_LOCK_MANUAL=1, MGL_LOCK_SMART=2 };
int FrameLockEnabled=0, MiniGLLibraryOpen=1;
void* NativeWindow=(void*)1;
int mode=0, lockCalls=0, unlockCalls=0, lockOK=1;
void mglLockMode(int value) { mode=value; }
int mglLockDisplay() { ++lockCalls; return lockOK; }
void mglUnlockDisplay() { ++unlockCalls; }
''' + filters + locking + r'''
int main() {
    FCachedTexture a={0,0}, b={0,0};
    SetCachedTextureFilter(&a,10,10); SetCachedTextureFilter(&a,10,10);
    assert(calls==4); // perf=0 retains the original repeated GL calls
    GAmigaPerfMask=2;
    SetCachedTextureFilter(&a,10,10); assert(calls==4);
    SetCachedTextureFilter(&b,20,20); assert(calls==6);
    SetCachedTextureFilter(&a,10,10); assert(calls==6); // per-texture cache
    SetCachedTextureFilter(&a,10,20); assert(calls==8);
    assert(lastMin==10 && lastMag==20); // both parameters refreshed
    a.AppliedMinFilter=a.AppliedMagFilter=0; // upload invalidates cached state
    SetCachedTextureFilter(&a,10,20); assert(calls==10);
    AmigaMiniGLSetFrameLock(0); assert(mode==MGL_LOCK_SMART);
    assert(AmigaMiniGLBeginFrame()); AmigaMiniGLEndFrame();
    assert(lockCalls==0 && unlockCalls==0);
    AmigaMiniGLSetFrameLock(1); assert(mode==MGL_LOCK_MANUAL);
    assert(AmigaMiniGLBeginFrame()); AmigaMiniGLEndFrame();
    assert(lockCalls==1 && unlockCalls==1);
    lockOK=0; assert(!AmigaMiniGLBeginFrame());
    NativeWindow=0; assert(!AmigaMiniGLBeginFrame()); assert(lockCalls==2);
    MiniGLLibraryOpen=0; AmigaMiniGLEndFrame(); assert(unlockCalls==1);
}
'''
with tempfile.TemporaryDirectory(prefix="ue-classic-perf-test-") as temp:
    executable = str(Path(temp) / "perf-test")
    subprocess.run(["c++", "-x", "c++", "-std=c++11", "-O2", "-Wall",
                    "-Wextra", "-", "-o", executable], input=program, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: filter pairs/cache/invalidation, frame locks/failure, viewport ordering")
