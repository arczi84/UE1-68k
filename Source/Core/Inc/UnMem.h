/*=============================================================================
	UnMem.h: FMemStack class, ultra-fast temporary memory allocation
	Copyright 1997 Epic MegaGames, Inc. This software is a trade secret.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

/*-----------------------------------------------------------------------------
	Globals.
-----------------------------------------------------------------------------*/

// Enums for specifying memory allocation type.
enum EMemZeroed {MEM_Zeroed=1};
enum EMemOned   {MEM_Oned  =1};

#ifdef UE_ALLOC_DIAG
struct FAllocDiagContext
{
	char Actor[96], Class[96], Mesh[96];
	const char* Stage;
	INT Verts, Tris, Particles;
	const void* ActorAddress;
	const void* MeshAddress;
	INT ActorSize, MeshSize, MeshOffset, FrameVertsOffset, TrisOffset;
	INT ActorIndex, MeshIndex, ActorRegistered, MeshRegistered;
};
extern "C" int AmigaAllocReadable(const void* Address, unsigned long Size);
extern CORE_API FAllocDiagContext GAllocDiag;
struct FDrawMeshCallContext
{
	const void* Actor;
	const void* Frame;
	const void* Sprite;
	const char* Site;
};
extern CORE_API FDrawMeshCallContext GDrawMeshCall;
struct FDrawMeshCallScope
{
	FDrawMeshCallContext Saved;
	FDrawMeshCallScope(const void* Actor,const void* Frame,const void* Sprite,const char* Site)
		: Saved(GDrawMeshCall)
	{
		GDrawMeshCall.Actor=Actor; GDrawMeshCall.Frame=Frame;
		GDrawMeshCall.Sprite=Sprite; GDrawMeshCall.Site=Site;
	}
	~FDrawMeshCallScope() { GDrawMeshCall=Saved; }
};
CORE_API void appAllocDiagFailure( INT Size, const char* Reason );
struct FAllocDiagScope
{
	FAllocDiagContext Saved;
	FAllocDiagScope() : Saved(GAllocDiag) {}
	~FAllocDiagScope() { GAllocDiag = Saved; }
};
inline INT AllocDiagBytes( size_t Size, INT Count )
{
	if( Count<0 || (Size && (size_t)Count > 0x7fffffffUL/Size) )
	{
		appAllocDiagFailure(Count,"element count overflow");
		appErrorf("Invalid allocation: count=%d element=%lu",Count,(unsigned long)Size);
	}
	return Size*Count;
}
#else
#define AllocDiagBytes(Size,Count) ((Size)*(Count))
#endif

/*-----------------------------------------------------------------------------
	FMemStack.
-----------------------------------------------------------------------------*/

//
// Simple linear-allocation memory stack.
// Items are allocated via PushBytes() or the specialized operator new()s.
// Items are freed en masse by using FMemMark to Pop() them.
//
class CORE_API FMemStack
{
public:
	// Get bytes.
	inline BYTE* PushBytes( INT AllocSize, INT Align )
	{
		// Debug checks.
		guardSlow(FMemStack::PushBytes);
#ifdef UE_ALLOC_DIAG
		if( AllocSize<0 || Align<=0 || (Align&(Align-1)) || AllocSize>0x7fffffff-Align )
		{
			appAllocDiagFailure(AllocSize,"PushBytes size/alignment");
			appErrorf("Invalid stack allocation: bytes=%d align=%d",AllocSize,Align);
		}
#endif
		debug(AllocSize>=0);
		debug((Align&(Align-1))==0);
		debug(Top<=End);

		// Try to get memory from the current chunk.
		BYTE* Result = (BYTE *)(((INT)Top+(Align-1))&~(Align-1));
		Top = Result + AllocSize;

		// Make sure we didn't overflow.
		if( Top > End )
		{
			// We'd pass the end of the current chunk, so allocate a new one.
			AllocateNewChunk( AllocSize + Align );
			Result = (BYTE *)(((int)Top+(Align-1))&~(Align-1));
			Top    = Result + AllocSize;
		}
		return Result;
		unguardSlow;
	}

	// Main functions.
	void Init( INT DefaultChunkSize );
	void Exit();
	void Tick();
	int  GetByteCount();

	// Friends.
	friend class FMemMark;
	friend void* operator new( size_t Size, FMemStack& Mem, INT Count, INT Align );
	friend void* operator new( size_t Size, FMemStack& Mem, EMemZeroed Tag, INT Count, INT Align );
	friend void* operator new( size_t Size, FMemStack& Mem, EMemOned Tag, INT Count, INT Align );

private:
	// Constants.
	enum {MAX_CHUNKS=1024};

	// Types.
	struct FTaggedMemory
	{
		FTaggedMemory* Next;
		INT DataSize;
		BYTE Data[];
	};

	// Variables.
	FMemCache*		GCache;				// The memory cache we use for chunk allocation.
	BYTE*			Top;				// Top of current chunk (Top<=End).
	BYTE*			End;				// End of current chunk.
	INT				DefaultChunkSize;	// Maximum chunk size to allocate.
	FTaggedMemory*	TopChunk;			// Only chunks 0..ActiveChunks-1 are valid.

	// Static.
	static FTaggedMemory* UnusedChunks;

	// Functions.
	BYTE* AllocateNewChunk( INT MinSize );
	void FreeChunks( FTaggedMemory* NewTopChunk );
};

/*-----------------------------------------------------------------------------
	FMemStack templates.
-----------------------------------------------------------------------------*/

// Operator new for typesafe memory stack allocation.
template <class T> inline T* New( FMemStack& Mem, INT Count=1, INT Align=DEFAULT_ALIGNMENT )
{
	guardSlow(FMemStack::New);
	return (T*)Mem.PushBytes( AllocDiagBytes(sizeof(T),Count), Align );
	unguardSlow;
}
template <class T> inline T* NewZeroed( FMemStack& Mem, INT Count=1, INT Align=DEFAULT_ALIGNMENT )
{
	guardSlow(FMemStack::New);
	BYTE* Result = Mem.PushBytes( AllocDiagBytes(sizeof(T),Count), Align );
	appMemset( Result, 0, Count*sizeof(T) );
	return (T*)Result;
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	FMemStack operator new's.
-----------------------------------------------------------------------------*/

// Operator new for typesafe memory stack allocation.
inline void* operator new( size_t Size, FMemStack& Mem, INT Count=1, INT Align=DEFAULT_ALIGNMENT )
{
	// Get uninitialized memory.
	guardSlow(FMemStack::New1);
	return Mem.PushBytes( AllocDiagBytes(Size,Count), Align );
	unguardSlow;
}
inline void* operator new( size_t Size, FMemStack& Mem, EMemZeroed Tag, INT Count=1, INT Align=DEFAULT_ALIGNMENT )
{
	// Get zero-filled memory.
	guardSlow(FMemStack::New2);
	BYTE* Result = Mem.PushBytes( AllocDiagBytes(Size,Count), Align );
	appMemset( Result, 0, Size*Count );
	return Result;
	unguardSlow;
}
inline void* operator new( size_t Size, FMemStack& Mem, EMemOned Tag, INT Count=1, INT Align=DEFAULT_ALIGNMENT )
{
	// Get one-filled memory.
	guardSlow(FMemStack::New3);
	BYTE* Result = Mem.PushBytes( AllocDiagBytes(Size,Count), Align );
	appMemset( Result, 255, Size*Count );
	return Result;
	unguardSlow;
}

/*-----------------------------------------------------------------------------
	FMemMark.
-----------------------------------------------------------------------------*/

//
// FMemMark marks a top-of-stack position in the memory stack.
// When the marker is constructed or initialized with a particular memory 
// stack, it saves the stack's current position. When marker is popped, it
// pops all items that were added to the stack subsequent to initialization.
//
class CORE_API FMemMark
{
public:
	// Constructors.
	FMemMark()
	{}
	FMemMark( FMemStack& InMem )
	{
		guardSlow(FMemMark::FMemMark);
		Mem          = &InMem;
		Top          = Mem->Top;
		SavedChunk   = Mem->TopChunk;
		unguardSlow;
	}

	// FMemMark interface.
	void Pop()
	{
		// Check state.
		guardSlow(FMemMark::Pop);

		// Unlock any new chunks that were allocated.
		if( SavedChunk != Mem->TopChunk )
			Mem->FreeChunks( SavedChunk );

		// Restore the memory stack's state.
		Mem->Top = Top;
		unguardSlow;
	}

private:
	// Implementation variables.
	FMemStack* Mem;
	BYTE* Top;
	FMemStack::FTaggedMemory* SavedChunk;
};

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
