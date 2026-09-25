#!/usr/bin/env python3
import pathlib
import subprocess
import tempfile

root=pathlib.Path(__file__).resolve().parents[2]
source=(root/'Source/Core/Src/UnFile.cpp').read_text()
start=source.index('void FArray::Realloc( INT ElementSize )')
method=source[start:source.index('\n}',start)+2]
header=(root/'Source/Core/Inc/UnArrayDiag.h').read_text()
mocks=r'''
#include <cassert>
#include <cstring>
#include <cstdint>
#include <climits>
#include <stdexcept>
using INT=int; using DWORD=uint32_t;
#define CORE_API
#define UE_ARRAY_DIAG
#define guard(x)
#define unguardf(x)
struct FArchive {};
void appStrncpy(char* d,const char* s,int n){strncpy(d,s,n);d[n-1]=0;}
struct FArray { void* Data;INT ArrayNum,ArrayMax;void Realloc(INT); };
int calls=0,lastSize=0;
void* appRealloc(void* p,INT n,const char*){++calls;lastSize=n;return p;}
void ReportBadArray(const char*,void*,void*,INT,INT,INT,DWORD,void*){throw std::runtime_error("bad array");}
'''
tests=r'''
FArrayDiagContext GArrayDiagContext={"none","none",nullptr,0,0};
int main(){
 for(auto size:{1,4,88}){
  FArray a={nullptr,3,4};a.Realloc(size);assert(lastSize==4*size);
 }
 FArray zero={nullptr,0,0};zero.Realloc(88);assert(lastSize==0);
 auto bad=[](int num,int max,int elem){
  FArray a={nullptr,num,max};int before=calls;bool caught=false;
  try{a.Realloc(elem);}catch(const std::runtime_error&){caught=true;}
  assert(caught && calls==before);
 };
 bad(-1,1,4);bad(1,-1,4);bad(5,4,4);bad(0,0,0);bad(0,0,-1);
 bad(1,INT_MAX,4);bad(1,INT_MAX/88+1,88);
 FArchive one,two;
 {FArrayDiagScope a("file1","object1",&one,10,20);
  {FArrayDiagScope b("file2","object2",&two,30,40);
   assert(GArrayDiagContext.Archive==&two);}
  assert(GArrayDiagContext.Archive==&one && !strcmp(GArrayDiagContext.Object,"object1"));
 }
 assert(GArrayDiagContext.Archive==nullptr && !strcmp(GArrayDiagContext.File,"none"));
}
'''
with tempfile.TemporaryDirectory(prefix='ue1-arraydiag-') as tmp:
 exe=str(pathlib.Path(tmp)/'test')
 subprocess.run(['g++','-x','c++','-std=c++14','-O2','-fsanitize=address,undefined',
                 '-fno-pie','-no-pie','-o',exe,'-'],input=mocks+header+method+tests,text=True,check=True)
 subprocess.run([exe],check=True)
print('Array diagnostic: invalid counts/overflow rejected, normal realloc and nested context passed.')
