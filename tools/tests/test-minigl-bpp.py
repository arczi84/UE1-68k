#!/usr/bin/env python3
"""Compile the actual parameter-to-native-window helper with mocked dependencies."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
viewport = (root / "Source/NSDLDrv/Src/NSDLViewport.cpp").read_text()
native = (root / "Source/NSDLDrv/Src/AmigaMiniGLWindow.c").read_text()
start = viewport.index("static INT OpenNativeMiniGLWindow(")
end = viewport.index("\n}", start) + 2
helper = viewport[start:end]
# Both initial creation and recreation must pass through the parser.
assert viewport.count("if( !OpenNativeMiniGLWindow(") == 2
assert viewport.count("AmigaMiniGLOpenWindow(") == 1
open_body = native[native.index("int AmigaMiniGLOpenWindow("):native.index("void AmigaMiniGLCloseWindow(")]
assert "ColorBits != 16 && ColorBits != 32" in open_body
assert "mglChoosePixelDepth( 16 )" not in open_body
assert open_body.index("mglChoosePixelDepth( ColorBits )") < open_body.index("mglCreateContext(")

mocks = r'''
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
using INT=int; using UBOOL=int;
enum {NAME_Log};
static const char* cmd="";
static int calls=0,bits=0,w=0,h=0,fs=0,result=1;
const char* appCmdLine() { return cmd; }
bool Parse(const char* s,const char* key,INT& value) {
    const char* p=strstr(s,key); if(!p) return false;
    value=atoi(p+strlen(key)); return true;
}
[[noreturn]] void appErrorf(const char*,...) { throw std::runtime_error("bad depth"); }
void debugf(int,const char*,...) {}
int AmigaMiniGLOpenWindow(int width,int height,int full,int depth) {
    ++calls;w=width;h=height;fs=full;bits=depth;return result;
}
'''
tests = r'''
int main() {
    assert(OpenNativeMiniGLWindow(800,600,0)==1);
    assert(bits==16 && w==800 && h==600 && fs==0);
    cmd="-MGLBPP=32"; OpenNativeMiniGLWindow(1024,768,1);
    assert(bits==32 && fs==1 && w==1024 && h==768);
    OpenNativeMiniGLWindow(640,480,0); assert(bits==32 && fs==0);
    cmd="-MGLBPP=16"; OpenNativeMiniGLWindow(800,600,1); assert(bits==16);
    for(const char* bad:{"-MGLBPP=0","-MGLBPP=24","-MGLBPP=-1","-MGLBPP=garbage"}) {
        cmd=bad; int old=calls; bool caught=false;
        try { OpenNativeMiniGLWindow(800,600,0); }
        catch(const std::runtime_error&) { caught=true; }
        assert(caught && calls==old);
    }
    cmd="-MGLBPP=32"; result=0; assert(OpenNativeMiniGLWindow(800,600,1)==0);
}
'''
with tempfile.TemporaryDirectory(prefix="ue1-bpp-") as tmp:
    binary = str(pathlib.Path(tmp) / "test")
    subprocess.run(["g++", "-x", "c++", "-std=c++14", "-Wall", "-Wextra",
                    "-O2", "-o", binary, "-"],
                   input=mocks + helper + tests, text=True, check=True)
    subprocess.run([binary], check=True)
print("MiniGL depth: default, 16/32, invalid values, recreation routing and API order passed.")
