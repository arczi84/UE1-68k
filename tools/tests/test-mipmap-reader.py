#!/usr/bin/env python3
"""Run extracted UE array/mipmap readers with the installed Amiga GCC in vamos."""
import os, pathlib, subprocess, tempfile
root=pathlib.Path(__file__).resolve().parents[2]
def read(p): return (root/p).read_text()
def method(s,start):
 a=s.index(start);return s[a:s.index('\n}',a)+2]+'\n'
t=read('Source/Core/Inc/UnTemplate.h')
a=t.index('class CORE_API FArray');b=t.index('/*-----------------------------------------------------------------------------',t.index('template <class T> void* operator new',a))
arrays=t[a:b]
c=read('Source/Core/Inc/Core.h')
a=c.index('template< class T > \ninline FArchive& operator<<');b=c.index('/*----------------------------------------------------------------------------',a)
arrayops=c[a:b]
arc=read('Source/Core/Inc/UnArc.h');arc=arc[arc.index('class CORE_API FArchive'):arc.index('\n};')+3]
mip=read('Source/Engine/Inc/UnTex.h');a=mip.index('struct ENGINE_API FMipmap');mip=mip[a:mip.index('\n};',a)+3]
u=read('Source/Core/Src/UnFile.cpp')
compact=method(read('Source/Core/Src/UnObj.cpp'),'FArchive& operator<<( FArchive& Ar, FCompactIndex& I )')
pre=r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
typedef int INT;typedef int UBOOL;typedef unsigned int DWORD;
typedef unsigned char BYTE;typedef char CHAR;typedef unsigned short _WORD;
typedef short SWORD;typedef float FLOAT;typedef unsigned long long QWORD;typedef long long SQWORD;
enum ENoInit { E_NoInit };
#define CORE_API
#define ENGINE_API
#define PACKAGE_FILE_VERSION 61
#define __INTEL_BYTE_ORDER__ 0
#define guard(x)
#define guardSlow(x)
#define unguard
#define unguardSlow
#define unguardf(x)
#define check(x) do{if(!(x)){printf("check failed %s line %d\n",#x,__LINE__);exit(2);}}while(0)
#define appFree free
#define appMemcpy memcpy
#define appMemset memset
#define appMemmove memmove
void* appRealloc(void* p,int n,const char*) {check(n>=0);if(!n){free(p);return 0;}void* q=realloc(p,n);check(q);return q;}
void appErrorf(const char* f,...) {va_list ap;va_start(ap,f);vprintf(f,ap);va_end(ap);exit(3);}
template<class T>T Abs(T a){return a>=0?a:-a;}
class FArchive;
struct FCompactIndex{INT Value;};
FArchive& operator<<(FArchive&,FCompactIndex&);
#define AR_INDEX(x) (*(FCompactIndex*)&(x))
'''
test=r'''
class Reader:public FArchive {
public:
 BYTE* bytes;int pos,size;
 Reader(BYTE* p,int n):bytes(p),pos(127200),size(n){ArIsLoading=1;}
 FArchive& Serialize(void* v,int n){check(n>=0 && pos+n<=size);memcpy(v,bytes+pos,n);pos+=n;return *this;}
 INT Tell(){return pos;}
};
__attribute__((noinline)) void load(FArchive& ar,TArray<FMipmap>& mips){ar<<mips;}
int main(int argc,char** argv){
 check(argc==2);FILE* f=fopen(argv[1],"rb");check(f);fseek(f,0,SEEK_END);int size=ftell(f);rewind(f);
 BYTE* bytes=(BYTE*)malloc(size);check(bytes);check(fread(bytes,1,size,f)==(size_t)size);fclose(f);
 Reader r(bytes,size);TArray<FMipmap> mips;load(r,mips);
 printf("mips=%d pos=%d expected=149153\n",mips.Num(),r.Tell());
 check(mips.Num()==9 && r.Tell()==149153);
 for(int i=0;i<9;++i){int w=64>>i,h=256>>i;if(w<1)w=1;
  printf("mip%d: %d bytes, %dx%d bits=%d,%d\n",i,mips(i).DataArray.Num(),mips(i).USize,mips(i).VSize,mips(i).UBits,mips(i).VBits);
  check(mips(i).DataArray.Num()==w*h && mips(i).USize==w && mips(i).VSize==h);
 }
 free(bytes);return 0;
}
'''
src=pre+arc+arrays+arrayops+method(u,'void FArray::Realloc( INT ElementSize )')+method(u,'void FArray::Remove( INT Index, INT Count, INT ElementSize )')+compact+mip+test
out=pathlib.Path(tempfile.mkdtemp(prefix='ue-mipreader-'))
print('Artifacts:',out,flush=True)
compiler=os.environ.get('UE_TEST_CXX','/opt/amiga/bin/m68k-amigaos-g++')
crt=os.environ.get('UE_TEST_CRT','-noixemul')
for mode in os.environ.get('UE_TEST_BBB_MODES','+,-').split(','):
 exe=out/('mip-'+mode.replace('+','on').replace('-','off'))
 extra=[] if mode=='default' else ['-fbbb='+mode]
 subprocess.run([compiler,'-x','c++','-std=gnu++14',crt,'-m68060','-mhard-float','-O3','-fomit-frame-pointer','-ffast-math','-fno-strict-aliasing','-fno-short-enums',*extra,'-Wl,--strip-all','-T'+str(root/'Source/cmake/amiga68k.ld'),'-o',str(exe),'-'],input=src,text=True,check=True)
 env=dict(os.environ,PYTHONPATH='/home/arczi/build/amiga13.4-1756a703/debug-python')
 result=subprocess.run(['python3','-m','amitools.tools.vamos','-S','-C','68040','-m','8192','-s','128',str(exe),'root:mnt/d/dev/UE1/Game/Unreal68k/System/Engine.u'],env=env)
 print('BBB',mode,'exit',result.returncode,flush=True)
 if result.returncode:
  raise SystemExit(result.returncode)
