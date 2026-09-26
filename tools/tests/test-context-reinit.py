#!/usr/bin/env python3
"""Exercise the real scene-state cache against a mock GL after context loss."""
import pathlib
import subprocess
import tempfile

root = pathlib.Path(__file__).resolve().parents[2]
source = (root / 'Source/NOpenGLDrv/NOpenGLDrv.cpp').read_text()
init = source.split('UBOOL UNOpenGLRenderDevice::Init(', 1)[1].split(
    'void UNOpenGLRenderDevice::Exit(', 1)[0]
assert init.index('Viewport = InViewport;') < init.index('SetSceneNode( NULL );')
method = source.split('void UNOpenGLRenderDevice::SetSceneNode(', 1)[1]
method = 'void UNOpenGLRenderDevice::SetSceneNode(' + method.split(
    'void UNOpenGLRenderDevice::SetBlend(', 1)[0]
harness = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
#define guard(x)
#define unguard
#define check(x) assert(x)
#define GL_PROJECTION 1
#define PI 3.14159265358979323846
static double appTan(double x) { return std::tan(x); }
static int viewports, projections;
static void glViewport(int,int,int,int) { ++viewports; }
static void glMatrixMode(int) {}
static void glLoadIdentity() {}
static void glFrustum(double,double,double,double,double,double) { ++projections; }
struct FSceneNode { int X,Y,XB,YB; float FX,FY; };
struct ActorType { float FovAngle; };
struct ViewportType { int SizeX,SizeY; ActorType* Actor; };
struct UNOpenGLRenderDevice {
    struct { int X,Y,XB,YB,SizeX,SizeY; float FX,FY,FovAngle; } CurrentSceneNode{};
    ViewportType* Viewport;
    float RProjZ,Aspect,RFX2,RFY2;
    void SetSceneNode(FSceneNode*);
};
'''
test = r'''
int main() {
    ActorType actor{90}; ViewportType viewport{800,600,&actor};
    FSceneNode frame{800,600,0,0,800,600};
    UNOpenGLRenderDevice renderer; renderer.Viewport=&viewport;
    renderer.SetSceneNode(NULL); renderer.SetSceneNode(&frame);
    assert(viewports==1 && projections==1);
    renderer.SetSceneNode(&frame);
    assert(viewports==1 && projections==1); // Normal frame remains cached.
    viewports=projections=0; // Fresh context; same dimensions and FOV.
    renderer.SetSceneNode(&frame);
    assert(viewports==0 && projections==0); // Reproduces stale-cache bug.
    for(int toggle=0; toggle<4; ++toggle) {
        renderer.SetSceneNode(NULL); // Init invalidates on each recreation.
        renderer.SetSceneNode(&frame);
        assert(viewports==toggle+1 && projections==toggle+1);
    }
    viewport.SizeX=frame.X=1024; viewport.SizeY=frame.Y=768;
    frame.FX=1024; frame.FY=768;
    renderer.SetSceneNode(NULL); renderer.SetSceneNode(&frame);
    assert(viewports==5 && projections==5);
    puts("PASS: context recreation restores viewport and projection; normal frame stays cached");
}
'''
with tempfile.TemporaryDirectory(prefix='ue-context-reinit-') as out:
    exe = str(pathlib.Path(out) / 'test')
    subprocess.run(['c++', '-x', 'c++', '-std=c++11', '-Wall', '-Wextra',
                    '-Werror', '-fsanitize=address,undefined', '-o', exe, '-'],
                   input=harness+method+test, text=True, check=True)
    subprocess.run([exe], check=True)
