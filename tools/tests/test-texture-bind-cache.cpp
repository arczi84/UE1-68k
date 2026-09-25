#include <cassert>
#include <cstdint>
#include <cstddef>
#include <vector>
#include <unordered_map>
#include <random>
#include <cstdio>

using INT=int;
using DWORD=uint32_t;
using QWORD=uint64_t;
// Exercise the production cache with a relocating host array. Engine TArray
// also relocates on growth; storing bucket-chain indices must survive this.
template<class T> class TArray
{
	std::vector<T> Data;
public:
	INT Num() const { return (INT)Data.size(); }
	T& operator()(INT I) { return Data.at(I); }
	const T& operator()(INT I) const { return Data.at(I); }
	void Empty() { Data.clear(); }
	void AddZeroed(INT N) { Data.resize(Data.size()+N,T{}); }
	INT AddItem(const T& V) { Data.push_back(V); return Num()-1; }
};
#include "../../Source/NOpenGLDrv/TextureBindCache.h"

struct Texture
{
	unsigned Id,Min,Mag;
	bool operator==(const Texture& B) const { return Id==B.Id && Min==B.Min && Mag==B.Mag; }
};

int main()
{
	TTextureBindCache<Texture> Cache;
	std::unordered_map<QWORD,Texture> Reference;
	assert(Cache.Size()==0 && !Cache.Find(0));
	// Zero, full-width keys, high-half-only changes and masked/unmasked pairs.
	std::vector<QWORD> Keys={0,~QWORD(0),QWORD(1)<<60,QWORD(1)<<63};
	for(unsigned I=1;I<4097;++I)
	{
		QWORD K=(QWORD(I)<<32)|(I*64U);
		Keys.push_back(K);
		Keys.push_back(K|(QWORD(1)<<60));
	}
	for(unsigned I=0;I<Keys.size();++I)
	{
		Texture V={I,1,2};
		assert(*Cache.Add(Keys[I],V)==V);
		Reference[Keys[I]]=V;
	}
	assert(Cache.Size()==(INT)Reference.size());
	for(auto& P:Reference) assert(Cache.Find(P.first) && *Cache.Find(P.first)==P.second);
	// Filter state mutations persist without re-inserting or growing the map.
	Texture* Bound=Cache.Find(Keys[100]);
	Bound->Min=7; Bound->Mag=8;
	Reference[Keys[100]]=*Bound;
	assert(Cache.Find(Keys[100])==Bound);
	const INT Before=Cache.Size();
	Texture Replacement={17,3,4};
	assert(Cache.Add(Keys[100],Replacement)==Bound);
	Reference[Keys[100]]=Replacement;
	assert(Cache.Size()==Before);
	assert(!Cache.Find(0x123456789abcdef0ULL));
	std::mt19937_64 Rng(17);
	for(INT I=0;I<20000;++I)
	{
		QWORD K=(I%3) ? Keys[Rng()%Keys.size()] : Rng();
		if(I%2)
		{
			Texture V={(unsigned)Rng(),(unsigned)Rng(),(unsigned)Rng()};
			Cache.Add(K,V); Reference[K]=V;
		}
		else
		{
			auto It=Reference.find(K);
			Texture* Value=Cache.Find(K);
			assert((Value!=nullptr)==(It!=Reference.end()));
			if(Value) assert(*Value==It->second);
		}
	}
	for(auto& P:Reference) assert(Cache.Find(P.first) && *Cache.Find(P.first)==P.second);
	Cache.Empty();
	assert(Cache.Size()==0);
	for(QWORD K:Keys) assert(!Cache.Find(K));
	Cache.Empty();
	assert(*Cache.Add(Keys[100],Replacement)==Replacement && Cache.Size()==1);
	puts("Texture cache growth, collisions, full-width keys, updates and flush/reuse passed.");
}
