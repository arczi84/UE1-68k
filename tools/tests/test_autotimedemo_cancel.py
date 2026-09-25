#!/usr/bin/env python3
"""Host regression test compiling the actual state/cancel/check code from UE."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[2]
source = (root / "Source/Engine/Src/UnGame.cpp").read_text()
start = source.index("// Unattended benchmark state.")
end = source.index("/*-----------------------------------------------------------------------------", start)
state = source[start:end]
start = source.index("\t// The script aborts the demo")
end = source.index("\t// Unattended benchmark: quit", start)
check = source[start:end]
assert end < source.index("if( AutoTimedemoDone || TimedOut )", end)
for function in ("UBOOL UGameEngine::Browse(", "ULevel* UGameEngine::LoadMap("):
    body = source[source.index(function):]
    assert "CancelAutoTimedemo(" in body[:body.index("Error256[0]=0;")]

program = r'''
#include <cassert>
#include <cstring>
typedef int UBOOL;
typedef int INT;
typedef double DOUBLE;
struct UObject {};
void debugf(const char*, ...) {}
int appStrncmp(const char* a,const char* b,int n) { return strncmp(a,b,n); }
void appStrncpy(char* a,const char* b,int n) { strncpy(a,b,n); }
#define ARRAY_COUNT(a) (sizeof(a)/sizeof((a)[0]))
''' + state + r'''
struct APlayerPawn { int bShowMenu; UObject* myHUD; } player;
struct Viewport { APlayerPawn* Actor; } viewport;
struct ViewportList {
    int Num() const { return 1; }
    Viewport* operator()(int) { return &viewport; }
};
struct ClientType { ViewportList Viewports; } client;
ClientType* Client = &client;
UObject demoHUD, normalHUD;
void CheckInterruption() {
''' + check + r'''
}
void Arm() {
    player.bShowMenu=0; player.myHUD=&demoHUD; viewport.Actor=&player;
    AutoTimedemoActive=1; AutoTimedemoDone=0; AutoTimedemoHUD=&demoHUD;
    AutoTimedemoStart=1; AutoTimedemoTimeout=300;
}
bool WouldExit(double now) {
    CheckInterruption();
    return AutoTimedemoActive && (AutoTimedemoDone || now-AutoTimedemoStart>=AutoTimedemoTimeout);
}
int main() {
    Arm(); assert(!WouldExit(100));
    AutoTimedemoNotifyMessage("Result: 45 FPS"); assert(WouldExit(101));
    Arm(); assert(WouldExit(301)); // genuinely unattended hang retains timeout
    Arm(); player.bShowMenu=1; assert(!WouldExit(301));
    player.bShowMenu=0; assert(!WouldExit(10000)); // no delayed exit in gameplay
    AutoTimedemoNotifyMessage("Result: late message"); assert(!AutoTimedemoDone);
    Arm(); AutoTimedemoDone=1; player.bShowMenu=1; assert(!WouldExit(302));
    Arm(); player.myHUD=&normalHUD; assert(!WouldExit(301));
    Arm(); player.myHUD=0; assert(!WouldExit(301));
    Arm(); viewport.Actor=0; assert(!WouldExit(301));
    Arm(); AutoTimedemoDone=1; CancelAutoTimedemo("level travel requested");
    assert(!WouldExit(10000)); assert(!AutoTimedemoHUD); assert(!AutoTimedemoDone);
    assert(AutoTimedemoStart==0); assert(AutoTimedemoWallFrames==0);
    CancelAutoTimedemo("repeated"); assert(!WouldExit(10001));
}
'''
with tempfile.TemporaryDirectory(prefix="ue-timedemo-test-") as temp:
    executable = str(Path(temp) / "cancel-test")
    subprocess.run(["c++", "-x", "c++", "-std=c++11", "-O2", "-Wall",
                    "-Wextra", "-", "-o", executable], input=program, text=True, check=True)
    subprocess.run([executable], check=True)
print("PASS: normal result/timeout, menu interruption, HUD removal, travel, late result")
