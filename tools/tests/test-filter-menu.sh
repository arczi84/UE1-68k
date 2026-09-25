#!/usr/bin/env bash
set -euo pipefail
ue_root=/mnt/d/dev/UE1-MiniGL
ue_testdir=$(mktemp -d /tmp/ue1-menu-test.XXXXXX)
{
  printf '%s\n' '#include <cassert>' '#include <cstring>' '#include <strings.h>' \
    '#include <cctype>' '#include <cstdarg>' '#include <cstdio>' '#include <string>' \
    '#define NOPENGLDRV_USE_MINIGL' 'typedef int UBOOL;' \
    'bool ParseCommand(const char** p,const char* word) { size_t n=strlen(word); if(strncasecmp(*p,word,n) || ((*p)[n] && !isspace((*p)[n]))) return false; *p+=n; while(isspace(**p)) ++*p; return true; }' \
    'bool ParseToken(const char*& p,char* b,int n,int) { int i=0; while(*p && !isspace(*p)) { if(i<n-1) b[i++]=*p; ++p; } b[i]=0; return i!=0; }' \
    'int appStricmp(const char* a,const char* b){return strcasecmp(a,b);}' \
    'struct FOutputDevice { std::string text; void Log(const char* s){text=s;} void Logf(const char* f,...){char b[256]; va_list a; va_start(a,f); vsnprintf(b,sizeof(b),f,a); va_end(a); text=b;} };' \
    'struct UNOpenGLRenderDevice { unsigned char TextureFilter=1; int saves=0; void SaveConfig(){++saves;} UBOOL Exec(const char*,FOutputDevice*); };'
  sed -n '/^UBOOL UNOpenGLRenderDevice::Exec(/,/^}/p' "$ue_root/Source/NOpenGLDrv/NOpenGLDrv.cpp"
  printf '%s\n' 'int main(){ UNOpenGLRenderDevice r; FOutputDevice out;' \
    'assert(r.Exec("GetTextureFiltering",&out) && out.text=="True");' \
    'assert(r.Exec("SetTextureFiltering Off",&out) && out.text=="False" && r.TextureFilter==0);' \
    'assert(r.Exec("GetTextureFiltering",&out) && out.text=="False");' \
    'assert(r.Exec("SetTextureFiltering On",&out) && out.text=="True" && r.TextureFilter==1);' \
    'assert(r.saves==2); r.Exec("MGLFILTER nearest",&out);' \
    'assert(r.Exec("GetTextureFiltering",&out) && out.text=="False");' \
    'r.Exec("MGLFILTER bilinear",&out); assert(r.TextureFilter==1 && r.saves==4);' \
    'r.Exec("SetTextureFiltering INVALID",&out); assert(r.TextureFilter==1 && r.saves==4);' \
    'assert(!r.Exec("UnrelatedCommand",&out)); }'
} | g++ -x c++ -O2 -Wall -Wextra -o "$ue_testdir/check" -
"$ue_testdir/check"
rg -q 'RenDev->Exec\("GetTextureFiltering",Out\)' "$ue_root/Source/NSDLDrv/Src/NSDLViewport.cpp"
rg -q 'RenDev->Exec\(Enabled \? "SetTextureFiltering On" : "SetTextureFiltering Off",Out\)' "$ue_root/Source/NSDLDrv/Src/NSDLViewport.cpp"
printf '%s\n' 'Menu command round-trip and viewport routing checks passed.'
