/* AmigaStack.c - guarantee a large stack at startup for the 68k build.
 *
 * UE1 recurses deeply (GC, object serialization, property init), so it needs a
 * big stack — the desktop builds bump it to 16 MB, the Vita build to 512 KB.
 * On AmigaOS the Shell/Workbench default stack is tiny (a few KB), and bebbo's
 * gcc 6.5 libnix ncrt0.o has NO __stack / StackSwap logic, so the usual
 * `unsigned long __stack = N;` trick does nothing. Instead we swap to a
 * freshly allocated stack via exec StackSwap() and run main() on it.
 *
 * Kept in its own C file (not a UE .cpp) so the AmigaOS NDK headers
 * (exec/types.h etc.) don't collide with the engine's own BYTE/WORD typedefs.
 * Pattern adapted from a known-good bebbo-gcc/libnix workaround.
 *
 * The StackSwapStruct must live in static storage — it must survive the swap
 * and must not sit on the stack being swapped away.
 */

#ifndef __amigaos__

int AmigaRunWithStack( int argc, const char** argv, int (*fn)( int, const char** ) )
{
	return fn( argc, argv );
}

#else

#include <exec/types.h>
#include <exec/tasks.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <utility/tagitem.h>
#include <exec/execbase.h>
int AmigaAllocReadable(const void* address, unsigned long size)
{
	unsigned long start=(unsigned long)address;
	struct MemHeader* header;
	int found=0;
	if(!start || !size || size>4096 || start>0xffffffffUL-(size-1)) return 0;
	/* Conservative full-range RAM check. No reads through a suspect pointer.
	 * RAM membership does not prove that it is still a live UObject. */
	Forbid();
	for(header=(struct MemHeader*)SysBase->MemList.lh_Head;
		header->mh_Node.ln_Succ;header=(struct MemHeader*)header->mh_Node.ln_Succ)
	{
		if(start>=(unsigned long)header->mh_Lower && start+size-1<(unsigned long)header->mh_Upper)
		{ found=1; break; }
	}
	Permit();
	return found;
}
#ifdef UE_ARRAY_DIAG
#include <proto/dos.h>
#include <string.h>
void AmigaArrayReport(const char* text)
{
	BPTR report=Open("PROGDIR:Unreal-array-diag.txt",MODE_NEWFILE);
	if(report) { Write(report,(APTR)text,strlen(text)); Close(report); }
	if(Output()) Write(Output(),(APTR)text,strlen(text));
}
#endif
#ifdef UE_ALLOC_DIAG
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
static void AllocDump(BPTR report,const char* label,const void* address,unsigned long size)
{
	unsigned long i,j,start=(unsigned long)address;
	char line[128];
	int used;
	if(size>2048) size=2048;
	used=snprintf(line,sizeof(line),"%s raw bytes (offset: hex), requested=%lu\n",label,size);
	Write(report,line,used);
	if(!start || start>0xffffffffUL-size) return;
	for(i=0;i<size;i+=16)
	{
		used=snprintf(line,sizeof(line),"%04lx:",i);
		for(j=i;j<i+16 && j<size;++j)
		{
			if(TypeOfMem((APTR)(start+j)))
				used+=snprintf(line+used,sizeof(line)-used," %02x",(unsigned int)*(volatile unsigned char*)(start+j));
			else
				used+=snprintf(line+used,sizeof(line)-used," ??");
		}
		line[used++]='\n';
		Write(report,line,used);
	}
}
void AmigaAllocReport(const char* text,const void* actor,unsigned long actor_size,const void* mesh,unsigned long mesh_size)
{
	/* Direct DOS writes: no fopen/malloc or engine logger during failure. */
	char memory[256];
	unsigned long free_bytes=AvailMem(MEMF_ANY);
	unsigned long largest=AvailMem(MEMF_ANY|MEMF_LARGEST);
	unsigned long fast_free=AvailMem(MEMF_FAST);
	unsigned long fast_largest=AvailMem(MEMF_FAST|MEMF_LARGEST);
	BPTR report;
	snprintf(memory,sizeof(memory),"free=%lu largest=%lu fast_free=%lu fast_largest=%lu\n",
		free_bytes,largest,fast_free,fast_largest);
	report=Open("PROGDIR:Unreal-alloc-raw.txt",MODE_NEWFILE);
	if(report)
	{
		Write(report,(APTR)text,strlen(text));
		Write(report,(APTR)memory,strlen(memory));
		AllocDump(report,"actor",actor,actor_size);
		AllocDump(report,"mesh",mesh,mesh_size);
		Close(report);
	}
	if(Output())
	{
		Write(Output(),(APTR)text,strlen(text));
		Write(Output(),(APTR)memory,strlen(memory));
	}
}
#endif
#ifdef UE_AMIGA_GPROF
#include <unistd.h>
extern void _moncleanup( void );
#endif

/* 8 MB — generous headroom for UE's deep recursion. */
#define UE_MIN_STACK (8UL * 1024UL * 1024UL)

/* Some startup code honors this even when libnix ncrt0.o does not. Harmless. */
unsigned long __stack = UE_MIN_STACK;

/* Static storage: survives the stack swap. */
static struct StackSwapStruct s_ss;
static APTR                   s_stack_mem;
static int                    s_entry_argc;
static const char**           s_entry_argv;
static int                  (*s_entry_fn)( int, const char** );

/* Diagnostic wrapper for the GL context created internally by SDL 1.2.
 * The final link uses --wrap so calls from SDL_cgxgl arrive here first. */
struct amigamesa_context;
extern void AmigaDebugLogf( const char* fmt, ... );
extern void AmigaRawMouseShutdown( void );
extern void AmigaLowLevelExit( int result ) __asm__("____exit") __attribute__((noreturn));
#ifndef AMIGA_USE_MINIGL
extern void EXIT_8_OpenLibs( void );
extern int GAmigaUseOpenGL;
extern struct amigamesa_context* __real_AmigaMesaCreateContext( struct TagItem* tags __asm("a0") );

struct amigamesa_context* __wrap_AmigaMesaCreateContext( struct TagItem* tags __asm("a0") )
{
	int i;
	struct amigamesa_context* result;
	AmigaDebugLogf( "[Amiga] Mesa: CreateContext tags=%p", tags );
	if( tags )
	{
		for( i = 0; i < 32; ++i )
		{
			AmigaDebugLogf( "[Amiga] Mesa: tag[%d]=0x%08lx data=0x%08lx",
				i, (unsigned long)tags[i].ti_Tag, (unsigned long)tags[i].ti_Data );
			if( tags[i].ti_Tag == TAG_DONE )
				break;
		}
	}
	AmigaDebugLogf( "[Amiga] Mesa: before real CreateContext" );
	result = __real_AmigaMesaCreateContext( tags );
	AmigaDebugLogf( "[Amiga] Mesa: after real CreateContext result=%p", result );
	return result;
}
#endif

static int running_under_os4( void )
{
	extern struct ExecBase* SysBase;
	return SysBase && SysBase->LibNode.lib_Version >= 50;
}

#include <stdio.h>
#ifdef UE_AMIGA_STACK_DEBUG
static void stacklog( const char* msg, unsigned long v )
{
	FILE* f = fopen( "PROGDIR:stack-debug.log", "a" );
	if( f ) { fprintf( f, "%s %lu\n", msg, v ); fclose( f ); }
}
#else
static void stacklog( const char* msg, unsigned long v )
{
	(void)msg;
	(void)v;
}
#endif

/* StackSwap changes SP in the middle of its caller.  Newer GCC versions may
 * still address that caller's arguments and locals relative to SP afterwards,
 * which then reads unrelated data from the new stack.  Enter a separate,
 * non-returning function immediately after the swap so its complete frame is
 * created on the new stack. */
static void AmigaReleaseSwapStackOnExit( void )
{
	/* Hand the 8MB swap stack to exec for cleanup on process exit.  It cannot
	   be freed directly while this function is still running on it. */
	if( s_stack_mem )
	{
		struct MemList* ml = (struct MemList*)AllocMem(
			sizeof(struct MemList), MEMF_PUBLIC | MEMF_CLEAR );
		if( ml )
		{
			ml->ml_NumEntries      = 1;
			ml->ml_ME[0].me_Addr   = s_stack_mem;
			ml->ml_ME[0].me_Length = UE_MIN_STACK;
			Forbid();
			AddTail( &FindTask(NULL)->tc_MemEntry, &ml->ml_Node );
			Permit();
			stacklog( "swap stack queued for exec cleanup:", UE_MIN_STACK );
		}
		else
		{
			stacklog( "swap stack LEAKED (no MemList):", UE_MIN_STACK );
		}
		s_stack_mem = NULL;
	}

}

/* Caller must first stop audio/input and close the native window. No UE GC,
 * config saving, or C++ destructors on this emergency path. */
void AmigaEmergencyProcessExit( int result ) __attribute__((noreturn));
void AmigaEmergencyProcessExit( int result )
{
	AmigaReleaseSwapStackOnExit();
	AmigaLowLevelExit( result );
}

static void AmigaRunOnSwappedStack( void ) __attribute__((noreturn,noinline));
static void AmigaRunOnSwappedStack( void )
{
	int result = s_entry_fn( s_entry_argc, s_entry_argv );
	AmigaRawMouseShutdown();
#ifndef AMIGA_USE_MINIGL
	if( GAmigaUseOpenGL )
	{
		AmigaDebugLogf( "[Amiga] stack: engine returned, closing GL libraries" );
		EXIT_8_OpenLibs();
	}
#endif
#ifdef UE_AMIGA_GPROF
	/* The swapped-stack path never returns to main(), so this is the only
	   place where the libnix profiler can be flushed before ____exit. */
	stacklog( "gprof cleanup:", 1 );
	chdir( "PROGDIR:" );
	_moncleanup();
	{
		FILE* profile = fopen( "PROGDIR:gmon.out", "rb" );
		if( profile )
		{
			long bytes;
			fseek( profile, 0, SEEK_END );
			bytes = ftell( profile );
			fclose( profile );
			stacklog( "gmon bytes:", (unsigned long)(bytes > 0 ? bytes : 0) );
		}
		else
		{
			stacklog( "gmon missing:", 1 );
		}
	}
#endif
	AmigaReleaseSwapStackOnExit();
	AmigaDebugLogf( "[Amiga] stack: clean low-level exit" );
	AmigaLowLevelExit( result );
}

int AmigaRunWithStack( int argc, const char** argv, int (*fn)( int, const char** ) )
{
	struct Task* self = FindTask( NULL );
	ULONG have = (ULONG)self->tc_SPUpper - (ULONG)self->tc_SPLower;
	s_stack_mem = NULL;
	stacklog( "have stack bytes:", have );
	stacklog( "os4:", running_under_os4() );
	if( have >= UE_MIN_STACK )
	{
		int result;
		stacklog( "using existing (big enough)", have );
		result = fn( argc, argv );
		AmigaRawMouseShutdown();
		return result;
	}

	/* On AmigaOS 4 the kernel validates stack bounds and may reject an
	   AllocMem() stack ("Stackpointer is out of bounds"); prefer the
	   launcher-provided stack there. On classic 68k, StackSwap is fine. */
	if( running_under_os4() )
	{
		int result = fn( argc, argv );
		AmigaRawMouseShutdown();
		return result;
	}

	s_stack_mem = AllocMem( UE_MIN_STACK, MEMF_ANY );
	if( !s_stack_mem )
		return fn( argc, argv );  /* run on the small stack rather than refuse */

	s_ss.stk_Lower   = s_stack_mem;
	s_ss.stk_Upper   = (ULONG)( (UBYTE*)s_stack_mem + UE_MIN_STACK );
	s_ss.stk_Pointer = (APTR)s_ss.stk_Upper;
	s_entry_argc     = argc;
	s_entry_argv     = argv;
	s_entry_fn       = fn;
	stacklog( "SWAPPING to new stack:", UE_MIN_STACK );
	StackSwap( &s_ss );

	/* This never returns.  Its prologue and all engine locals are created on
	   the freshly selected stack, rather than in this function's old frame. */
	AmigaRunOnSwappedStack();
}

#endif /* __amigaos__ */
