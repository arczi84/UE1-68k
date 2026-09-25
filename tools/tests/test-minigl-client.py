#!/usr/bin/env python3
"""Test actual UE MiniGL import code with controlled library/dispatch replies."""
import pathlib, subprocess, tempfile
root = pathlib.Path(__file__).resolve().parents[2]
source = (root / 'Source/NMiniGLDrv/MiniGLClient.c').read_text()
source = '\n'.join(line for line in source.splitlines()
                   if not line.startswith(('#include <proto/', '#include <libraries/')))
mock = r'''
#include <stddef.h>
#include <stdio.h>
#include <assert.h>
#include <string.h>
typedef unsigned long ULONG;
typedef int BOOL;
#define TRUE 1
#define FALSE 0
#define MINIGLNAME "minigl.library"
#define MINIGL_VERSION 14
#define MINIGL_DISPATCH_ABI_VERSION 3UL
struct Library { unsigned lib_Version, lib_Revision; } lib = {27,0};
typedef struct { ULONG abiVersion, structSize; char padding[624-2*sizeof(ULONG)];
    void *MGLCreateContextFromWindow; void *MGLCreateContextFromBitMap;
} MGLDispatchTable;
struct Library *MiniGLBase;
const MGLDispatchTable *MiniGLDispatch;
static MGLDispatchTable table;
static int unavailable, no_table, closes;
static struct Library *OpenLibrary(const char *name, int version) {
    assert(!strcmp(name, MINIGLNAME) && version == MINIGL_VERSION);
    return unavailable ? NULL : &lib;
}
static void CloseLibrary(struct Library *p) { assert(p == &lib); ++closes; }
const MGLDispatchTable *MiniGLGetDispatchTableLVO(void) { return no_table ? NULL : &table; }
'''
test = r'''
int main(void) {
    assert(offsetof(MGLDispatchTable, MGLCreateContextFromWindow)==624);
    unavailable=1; assert(!MiniGLOpen()); assert(strstr(UEMiniGLGetOpenError(),"OpenLibrary"));
    unavailable=0; no_table=1; assert(!MiniGLOpen()); assert(!MiniGLBase && !MiniGLDispatch);
    no_table=0; table.abiVersion=2; table.structSize=632;
    assert(!MiniGLOpen()); assert(strstr(UEMiniGLGetOpenError(),"ABI=2"));
    table.abiVersion=3; table.structSize=623;
    assert(!MiniGLOpen()); assert(strstr(UEMiniGLGetOpenError(),"size=623"));
    table.structSize=624; assert(MiniGLOpen()); assert(MiniGLDispatch==&table);
    assert(!*UEMiniGLGetOpenError()); assert(MiniGLOpen()); MiniGLClose();
    table.structSize=632; assert(MiniGLOpen()); MiniGLClose();
    assert(!MiniGLBase && !MiniGLDispatch && closes==5);
    puts("PASS: missing library/table, wrong ABI, short table rejected; 624/632 accepted; close/reset");
}
'''
with tempfile.TemporaryDirectory(prefix='ue-minigl-client-') as out:
    exe = str(pathlib.Path(out)/'test')
    subprocess.run(['cc','-x','c','-std=c99','-Wall','-Wextra','-Werror',
                    '-fsanitize=address,undefined','-o',exe,'-'],
                   input=mock+source+test,text=True,check=True)
    subprocess.run([exe],check=True)
