#pragma once

// Renderer-local QWORD cache. Bucket chains store indices, never pointers into
// Entries: TArray growth may relocate entries, just as with the original TMap.
// No GL state, texture lifetime or global Core container behaviour changes.
template<class TValue> class TTextureBindCache
{
	struct FEntry
	{
		QWORD Key;
		TValue Value;
		INT Next; // index + 1; zero terminates a chain
	};
	TArray<FEntry> Entries;
	TArray<INT> Buckets;

	static DWORD Hash(QWORD Key)
	{
		DWORD H=(DWORD)Key ^ (DWORD)(Key>>32);
		// Mix both halves, including the masked-texture tag at bit 60.
		H^=H>>16;
		H*=0x7feb352dU;
		H^=H>>15;
		H*=0x846ca68bU;
		return H^(H>>16);
	}
	void Rehash(INT Count)
	{
		Buckets.Empty();
		Buckets.AddZeroed(Count);
		for(INT I=0;I<Entries.Num();++I)
		{
			const INT Bucket=Hash(Entries(I).Key)&(Count-1);
			Entries(I).Next=Buckets(Bucket);
			Buckets(Bucket)=I+1;
		}
	}
public:
	INT Size() const { return Entries.Num(); }
	TValue* Find(const QWORD& Key)
	{
		if(!Buckets.Num()) return NULL;
		for(INT Link=Buckets(Hash(Key)&(Buckets.Num()-1));Link;Link=Entries(Link-1).Next)
			if(Entries(Link-1).Key==Key) return &Entries(Link-1).Value;
		return NULL;
	}
	TValue* Add(const QWORD& Key,const TValue& Value)
	{
		TValue* Existing=Find(Key);
		if(Existing) { *Existing=Value; return Existing; }
		if(!Buckets.Num()) Rehash(256);
		else if(Entries.Num()/2>=Buckets.Num()) Rehash(Buckets.Num()*2);
		const INT Bucket=Hash(Key)&(Buckets.Num()-1);
		FEntry Entry;
		Entry.Key=Key;
		Entry.Value=Value;
		Entry.Next=Buckets(Bucket);
		const INT Index=Entries.AddItem(Entry);
		Buckets(Bucket)=Index+1;
		return &Entries(Index).Value;
	}
	void Empty()
	{
		Entries.Empty();
		Buckets.Empty();
	}
};
