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

/* Diagnostic wrapper for the GL context created internally by SDL 1.2.
 * The final link uses --wrap so calls from SDL_cgxgl arrive here first. */
struct amigamesa_context;
extern void AmigaDebugLogf( const char* fmt, ... );
extern void EXIT_8_OpenLibs( void );
extern int GAmigaUseOpenGL;
extern void AmigaRawMouseShutdown( void );
extern void AmigaLowLevelExit( int result ) __asm__("____exit") __attribute__((noreturn));
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

int AmigaRunWithStack( int argc, const char** argv, int (*fn)( int, const char** ) )
{
	struct Task* self = FindTask( NULL );
	ULONG have = (ULONG)self->tc_SPUpper - (ULONG)self->tc_SPLower;
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
	stacklog( "SWAPPING to new stack:", UE_MIN_STACK );
	StackSwap( &s_ss );

	/* Running on the new stack now.  A second StackSwap corrupts the return
	   path under this libnix/WinUAE combination (the engine has already shut
	   down successfully at that point).  Close the manually opened GL libraries
	   and use libnix's low-level exit, which restores the original process SP
	   without walking the incompatible stock C++ destructor list. */
	{
		int result = fn( argc, argv );
		AmigaRawMouseShutdown();
		if( GAmigaUseOpenGL )
		{
			AmigaDebugLogf( "[Amiga] stack: engine returned, closing GL libraries" );
			EXIT_8_OpenLibs();
		}
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
		/* Hand the 8MB swap stack back before leaving.  ____exit restores the
		   process's original stack pointer, so the block is only still in use
		   up to that call -- but FreeMem() cannot be the last thing to touch
		   it, because we are running on it right now.  Swapping back first is
		   what the comment above warns about, so instead the block is released
		   through the task's memory list, which exec frees when the process
		   goes away.  Without this the 8MB stays allocated until reboot. */
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

		AmigaDebugLogf( "[Amiga] stack: clean low-level exit" );
		AmigaLowLevelExit( result );
	}
}

#endif /* __amigaos__ */
