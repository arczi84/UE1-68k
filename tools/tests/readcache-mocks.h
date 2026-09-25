#include <algorithm>
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>
using INT=int;
using BYTE=unsigned char;
struct FArchive {};
struct FFileStatus { INT SavedPos; };
#define guard(x)
#define unguard
#define guardSlow(x)
#define unguardSlow
#define check(x) assert(x)
#define USEEK_END SEEK_END
#define USEEK_SET SEEK_SET
#define appStrcpy std::strcpy
#define appMemcpy std::memcpy
#define appFclose std::fclose
#define appFtell std::ftell
#define appFerror std::ferror
#define appFree std::free
template<class T> T Min(T a,T b) { return std::min(a,b); }
static const char* options="";
const char* appCmdLine() { return options; }
bool Parse(const char* s,const char* key,INT& value) {
    const char* found=std::strstr(s,key);
    if(!found) return false;
    value=std::atoi(found+std::strlen(key)); return true;
}
const char* LocalizeError(const char* s) { return s; }
[[noreturn]] void appErrorf(const char*,...) { throw std::runtime_error("read error"); }
[[noreturn]] void appThrowf(const char*) { throw std::runtime_error("open error"); }
void* appMalloc(INT n,const char*) { void* p=std::malloc(n); assert(p); return p; }
static std::vector<BYTE> fixture;
static int reads=0,seeks=0,shortRead=0,failSeek=0;
FILE* appFopen(const char*,const char*) {
    FILE* f=std::tmpfile(); assert(f);
    assert(std::fwrite(fixture.data(),1,fixture.size(),f)==fixture.size());
    std::rewind(f); return f;
}
INT appFseek(FILE* f,INT pos,INT mode) {
    ++seeks; if(failSeek) return -1; return std::fseek(f,pos,mode);
}
INT appFread(void* p,INT size,INT count,FILE* f) {
    ++reads; if(shortRead) return 0;
    return std::fread(p,size,count,f);
}
