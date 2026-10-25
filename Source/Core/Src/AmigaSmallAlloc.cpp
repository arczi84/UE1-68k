/*
 * Size-class allocator for appMalloc on AmigaOS (UE_AMIGA_SMALLALLOC).
 *
 * libnix malloc() calls exec Allocate() on every MemHeader in its block list
 * until one fits, and free() searches that list and returns emptied blocks to
 * the system with FreeMem().  Unreal performs thousands of new/delete per
 * frame, so frames spend much of their time inside exec's chunk walks, and
 * the system free list (fragmented by earlier runs) is walked again whenever
 * a block is re-acquired.  Here small requests are served from per-size free
 * lists carved out of 64 KB slabs: O(1), no exec call except for a new slab.
 * Slabs are only returned to the system at program exit.
 *
 * Kept free of Unreal headers: exec's BYTE/WORD typedefs clash with Core's.
 */
#include <exec/memory.h>
#include <proto/exec.h>
#include <stdlib.h>

#define SMALL_MAGIC     0xAE5A0000UL
#define SMALL_MAGICMASK 0xFFFF0000UL
#define SLAB_BYTES      (64 * 1024)

/* Payload sizes; each block carries one ULONG header before the payload. */
static const unsigned long ClassSizes[] = {
	8, 16, 24, 32, 48, 64, 80, 96, 128, 160, 192, 256, 320, 384, 512,
	640, 768, 1024, 1280, 1536, 2048
};
#define NUM_CLASSES (sizeof(ClassSizes) / sizeof(ClassSizes[0]))
#define MAX_SMALL   2048

struct FreeBlock { struct FreeBlock *Next; };
struct Slab { struct Slab *Next; };

static struct FreeBlock *FreeLists[NUM_CLASSES];
static struct Slab *Slabs;
static unsigned char SizeToClass[MAX_SMALL / 8 + 1];
static int Initialized, ShutDown;

static void ReleaseSlabs(void)
{
	/* Late frees from static destructors must not touch released memory. */
	ShutDown = 1;
	while (Slabs)
	{
		struct Slab *Next = Slabs->Next;
		FreeMem(Slabs, SLAB_BYTES);
		Slabs = Next;
	}
}

static void Init(void)
{
	unsigned long Size, Class = 0;
	for (Size = 0; Size <= MAX_SMALL; Size += 8)
	{
		while (ClassSizes[Class] < Size)
			++Class;
		SizeToClass[Size / 8] = (unsigned char)Class;
	}
	atexit(ReleaseSlabs);
	Initialized = 1;
}

/* Called with task switching disabled. */
static int Refill(unsigned long Class)
{
	const unsigned long Stride = ClassSizes[Class] + sizeof(unsigned long);
	struct Slab *S = (struct Slab *)AllocMem(SLAB_BYTES, MEMF_ANY);
	unsigned char *P, *End;
	if (!S)
		return 0;
	S->Next = Slabs;
	Slabs = S;
	/* The slab link occupies the first 8 bytes; keep payloads 4-aligned. */
	P = (unsigned char *)S + 8;
	End = (unsigned char *)S + SLAB_BYTES;
	for (; P + Stride <= End; P += Stride)
	{
		*(unsigned long *)P = SMALL_MAGIC | Class;
		struct FreeBlock *B = (struct FreeBlock *)(P + sizeof(unsigned long));
		B->Next = FreeLists[Class];
		FreeLists[Class] = B;
	}
	return 1;
}

extern "C" void *AmigaSmallAlloc(unsigned long Size)
{
	if (Size > MAX_SMALL || ShutDown)
		return malloc(Size);
	if (!Initialized)
		Init();
	const unsigned long Class = SizeToClass[(Size + 7) / 8];
	struct FreeBlock *B;
	/* The SDL audio task may also allocate; Forbid is cheaper than a semaphore. */
	Forbid();
	if (!FreeLists[Class] && !Refill(Class))
	{
		Permit();
		return malloc(Size);
	}
	B = FreeLists[Class];
	FreeLists[Class] = B->Next;
	Permit();
	return B;
}

/* libnix stores the chunk size (< 2 GB) in the same slot, so it never matches. */
extern "C" int AmigaSmallOwns(const void *Ptr)
{
	return (((const unsigned long *)Ptr)[-1] & SMALL_MAGICMASK) == SMALL_MAGIC;
}

extern "C" unsigned long AmigaSmallCapacity(const void *Ptr)
{
	return ClassSizes[((const unsigned long *)Ptr)[-1] & 0xFF];
}

extern "C" void AmigaSmallFree(void *Ptr)
{
	if (!AmigaSmallOwns(Ptr))
	{
		free(Ptr);
		return;
	}
	if (ShutDown)
		return;
	const unsigned long Class = ((unsigned long *)Ptr)[-1] & 0xFF;
	struct FreeBlock *B = (struct FreeBlock *)Ptr;
	Forbid();
	B->Next = FreeLists[Class];
	FreeLists[Class] = B;
	Permit();
}

#ifdef UE_AMIGA_SMALLALLOC
/*
 * Core's inline operator new maps to appMalloc, but the link pulls operator
 * delete, new[] and delete[] from libstdc++ (libnix malloc/free).  libnix
 * free() returns without releasing its semaphore when the block is not in
 * its list, so deleting one of our blocks there deadlocks the next malloc
 * from another task (the SDL audio task).  Strong definitions here replace
 * the library members, keeping every global new/delete on this allocator.
 */
#include <new>

void *operator new(size_t Size)
{
	void *Ptr = AmigaSmallAlloc(Size ? Size : 1);
	if (!Ptr)
		abort();
	return Ptr;
}
void *operator new[](size_t Size)
{
	return operator new(Size);
}
void *operator new(size_t Size, const std::nothrow_t &) throw()
{
	return AmigaSmallAlloc(Size ? Size : 1);
}
void *operator new[](size_t Size, const std::nothrow_t &) throw()
{
	return AmigaSmallAlloc(Size ? Size : 1);
}
void operator delete(void *Ptr) throw()
{
	if (Ptr)
		AmigaSmallFree(Ptr);
}
void operator delete[](void *Ptr) throw()
{
	if (Ptr)
		AmigaSmallFree(Ptr);
}
void operator delete(void *Ptr, size_t) throw()
{
	if (Ptr)
		AmigaSmallFree(Ptr);
}
void operator delete[](void *Ptr, size_t) throw()
{
	if (Ptr)
		AmigaSmallFree(Ptr);
}
void operator delete(void *Ptr, const std::nothrow_t &) throw()
{
	if (Ptr)
		AmigaSmallFree(Ptr);
}
void operator delete[](void *Ptr, const std::nothrow_t &) throw()
{
	if (Ptr)
		AmigaSmallFree(Ptr);
}
#endif
