#include "SDL2/SDL.h"
#ifdef PLATFORM_WIN32
#include <windows.h>
#endif
#ifdef PLATFORM_PSVITA
#include <vitasdk.h>
#include <vitaGL.h>
#include <unistd.h>
#endif

#include "Engine.h"

extern CORE_API FGlobalPlatform GTempPlatform;
extern DLL_IMPORT UBOOL GTickDue;
extern "C" {HINSTANCE hInstance;}
extern "C" {char GCC_HIDDEN THIS_PACKAGE[64]="Launch";}
#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
// Shared with realtime/procedural textures, whose legacy DOUBLE->FLOAT timing
// path is unreliable with this 68k soft-float runtime.
FLOAT GAmigaFrameDeltaSeconds = 1.0f / 30.0f;
static INT GAmigaTimingTraceRemaining = 4;
#endif

// FExecHook.
class FExecHook : public FExec
{
	UBOOL Exec( const char* Cmd, FOutputDevice* Out )
	{
		return 0;
	}
};

FExecHook GLocalHook;
DLL_EXPORT FExec* GThisExecHook = &GLocalHook;

#ifdef PLATFORM_PSVITA

//
// PSVita-specific globals.
//

#define MAX_PATH 1024
#define SYSTEM_PATH "data/unreal/System/"

// 200MB libc heap, 512K main thread stack, 16MB for loading game DLLs
// the rest goes to vitaGL
extern "C" { SceUInt32 sceUserMainThreadStackSize = 512 * 1024; }
extern "C" { unsigned int _pthread_stack_default_user = 512 * 1024; }
extern "C" { unsigned int _newlib_heap_size_user = 200 * 1024 * 1024; }
#define VGL_MEM_THRESHOLD ( 16 * 1024 * 1024 )

static char GRootPath[MAX_PATH] = "app0:/";

//
// PSVita-specific functions.
//

static bool FindRootPath( char* Out, int OutLen )
{
	static const char *Drives[] = { "uma0", "imc0", "ux0" };

	// check if an unreal folder exists on one of the drives
	// default to the last one (ux0)
	for ( unsigned int i = 0; i < sizeof(Drives) / sizeof(*Drives); ++i )
	{
		snprintf( Out, OutLen, "%s:/" SYSTEM_PATH, Drives[i] );
		SceUID Dir = sceIoDopen( Out );
		if ( Dir >= 0 )
		{
			sceIoDclose( Dir );
			return true;
		}
	}

	// not found
	return false;
}

static INT PowerCallback( INT NotifyID, INT NotifyCnt, INT PowerInfo, void* Common )
{
	if ( PowerInfo & ( SCE_POWER_CB_APP_RESUME | SCE_POWER_CB_APP_RESUMING ) )
	{
		debugf( "PowerCallback: resuming..." );
		appHandleSuspendResume( false );
	}
	else if ( PowerInfo & ( SCE_POWER_CB_BUTTON_PS_PRESS | SCE_POWER_CB_APP_SUSPEND | SCE_POWER_CB_SYSTEM_SUSPEND ) )
	{
		debugf( "PowerCallback: suspending..." );
		appHandleSuspendResume( true );
	}

	return 0;
}

static INT CallbackThread( DWORD Argc, void* Argv )
{
	const INT CbID = sceKernelCreateCallback( "Power Callback", 0, PowerCallback, nullptr );
	scePowerRegisterCallback( CbID );
	while( true )
		sceKernelDelayThreadCB( 10000000 );
	return 0;
}

[[noreturn]] static void EarlyError( const char* Msg )
{
	fprintf( stderr, "FATAL ERROR: %s\n", Msg );
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, "Fatal Error", Msg, nullptr );
	sceKernelExitProcess( 0 );
	abort();
}

static void PlatformPreInit()
{
	sceTouchSetSamplingState( SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_STOP );
	scePowerSetArmClockFrequency( 444 );
	scePowerSetBusClockFrequency( 222 );
	scePowerSetGpuClockFrequency( 222 );
	scePowerSetGpuXbarClockFrequency( 166 );
	sceSysmoduleLoadModule( SCE_SYSMODULE_NET );

	if ( !FindRootPath( GRootPath, sizeof(GRootPath) ) )
		EarlyError( "Could not find Unreal directory" );

	if ( chdir( GRootPath ) < 0 )
		EarlyError( "Could not chdir to Unreal directory" );

	SceUID Th = sceKernelCreateThread( "CallbackThread", CallbackThread, 0x10000100, 0x10000, 0, 0, nullptr );
	if( Th >= 0 )
		sceKernelStartThread( Th, 0, nullptr );

	vglInitWithCustomThreshold( 0, 960, 544, VGL_MEM_THRESHOLD, 0, 0, 0, SCE_GXM_MULTISAMPLE_2X );
	vglSetSemanticBindingMode( VGL_MODE_POSTPONED );
}

#else

void PlatformPreInit()
{

}

#endif


//
// Handle an error.
//
void HandleError()
{
	GIsGuarded=0;
	GIsCriticalError=1;
	debugf( NAME_Exit, "Shutting down after catching exception" );
	GObj.ShutdownAfterError();
	debugf( NAME_Exit, "Exiting due to exception" );
	GErrorHist[ARRAY_COUNT(GErrorHist)-1]=0;
	SDL_ShowSimpleMessageBox( SDL_MESSAGEBOX_ERROR, LocalizeError("Critical"), GErrorHist, SDL_GetKeyboardFocus() );
}

//
// Initialize.
//
UEngine* InitEngine()
{
	guard(InitEngine);

	// Platform init.
	appInit();
	GDynMem.Init( 65536 );

	// Init subsystems.
	GSceneMem.Init( 32768 );

	// First-run menu.
	UBOOL FirstRun=0;
	GetConfigBool( "FirstRun", "FirstRun", FirstRun );

	// Create the global engine object.
	UClass* EngineClass;
	if( !GIsEditor )
	{
		// Create game engine.
		EngineClass = GObj.LoadClass( UGameEngine::StaticClass, NULL, "ini:Engine.Engine.GameEngine", NULL, LOAD_NoFail | LOAD_KeepImports, NULL );
	}
	else if( ParseParam( appCmdLine(),"MAKE" ) )
	{
		// Create editor engine.
		EngineClass = GObj.LoadClass( UEngine::StaticClass, NULL, "ini:Engine.Engine.EditorEngine", NULL, LOAD_NoFail | LOAD_DisallowFiles | LOAD_KeepImports, NULL );
	}
	else
	{
		// Editor.
		EngineClass = GObj.LoadClass( UEngine::StaticClass, NULL, "ini:Engine.Engine.EditorEngine", NULL, LOAD_NoFail | LOAD_KeepImports, NULL );
	}

	// Init engine.
	UEngine* Engine = ConstructClassObject<UEngine>( EngineClass );
	Engine->Init();

	return Engine;

	unguard;
}

//
// Unreal's main message loop.  All windows in Unreal receive messages
// somewhere below this function on the stack.
//
void MainLoop( UEngine* Engine )
{
	guard(MainLoop);

	GIsRunning = 1;
#ifdef PLATFORM_AMIGA
	DWORD AmigaOldTicks = SDL_GetTicks();
	DWORD AmigaFrameStart = AmigaOldTicks;
	DWORD AmigaEngineMillis = 0;
	INT AmigaFrameCount = 0;
#else
	DOUBLE OldTime = appSeconds();
#endif
	while( GIsRunning && !GIsRequestingExit )
	{
		// Update the world.
#ifdef PLATFORM_AMIGA
		const DWORD AmigaNewTicks = SDL_GetTicks();
		const DWORD AmigaDeltaMillis = AmigaNewTicks - AmigaOldTicks;
		AmigaOldTicks = AmigaNewTicks;
		AmigaEngineMillis += AmigaDeltaMillis;
		GAmigaFrameDeltaSeconds = (FLOAT)AmigaDeltaMillis * 0.001f;
		Engine->Tick( GAmigaFrameDeltaSeconds );
#else
		DOUBLE NewTime = appSeconds();
		Engine->Tick( NewTime - OldTime );
		OldTime = NewTime;
#endif

#ifdef PLATFORM_AMIGA
		if( GAmigaTimingTraceRemaining > 0 && ++AmigaFrameCount == 25 )
		{
			const DWORD WallMillis = SDL_GetTicks() - AmigaFrameStart;
			const DWORD FpsTimes100 = WallMillis ? (2500000UL / WallMillis) : 0;
			AmigaDebugLogf( "[Amiga] Timing: 25 frames wall=%lu ms engine=%lu ms fps100=%lu",
				(unsigned long)WallMillis, (unsigned long)AmigaEngineMillis, (unsigned long)FpsTimes100 );
			AmigaFrameStart = SDL_GetTicks();
			AmigaEngineMillis = 0;
			AmigaFrameCount = 0;
			--GAmigaTimingTraceRemaining;
		}
#endif

		// Enforce optional maximum tick rate.
		INT MaxTickRate = Engine->GetMaxTickRate();
		if( MaxTickRate )
		{
#ifdef PLATFORM_AMIGA
			const DWORD TargetMillis = 1000 / MaxTickRate;
			const DWORD UsedMillis = SDL_GetTicks() - AmigaOldTicks;
			if( UsedMillis < TargetMillis )
				SDL_Delay( TargetMillis - UsedMillis );
#else
			DOUBLE Delta = (1.0/MaxTickRate) - (appSeconds()-OldTime);
			if( Delta > 0.0 )
				appSleep( Delta );
#endif
		}
	}
	GIsRunning = 0;
	unguard;
}

//
// Exit the engine.
//
void ExitEngine( UEngine* Engine )
{
	guard(ExitEngine);

	GObj.Exit();
	GMem.Exit();
	GDynMem.Exit();
	GSceneMem.Exit();
	GCache.Exit(1);
	appDumpAllocs( &GTempPlatform );

	unguard;
}

#ifdef PLATFORM_WIN32
INT WINAPI WinMain( HINSTANCE hInInstance, HINSTANCE hPrevInstance, char* InCmdLine, INT nCmdShow )
#else
#ifdef PLATFORM_AMIGA
// libnix's crt (-noixemul) runs its own __INIT_LIST__ but never walks the
// GCC C++ __CTOR_LIST__, so global constructors — including every
// IMPLEMENT_CLASS's `UClass autoclassXxx(...)` that registers hardcoded FNames
// — never run, leaving the name table empty (assert Names.Num() at startup).
// Walk the list ourselves before anything else. GCC layout: [0] is a count or
// (unsigned)-1, followed by function pointers, 0-terminated. Handle both.
// Dense C++ constructor pointer array, bracketed by our custom linker script
// (Source/cmake/amiga68k.ld). Each entry is one global constructor.
typedef void (*CtorFn)();
extern "C" CtorFn __UE_CTOR_START[];
extern "C" CtorFn __UE_CTOR_END[];
extern "C" void AmigaResetIntrinsicRegistry();

static int GAmigaCtorsRun = 0; // diagnostic: how many ctors actually fired.

// Append a diagnostic line to a file next to the executable, so startup traces
// survive even when stderr goes to a console we can't capture. Opens/closes
// each call (flushes immediately) — startup is not perf-critical.
extern "C" void AmigaDebugLog( const char* Msg )
{
	FILE* F = fopen( "PROGDIR:startup-debug.log", "a" );
	if( F )
	{
		fputs( Msg, F );
		fputc( '\n', F );
		fclose( F );
	}
}

// printf-style variant used by the startup traces in other TUs.
#include <stdarg.h>
extern "C" void AmigaDebugLogf( const char* Fmt, ... )
{
	char Buf[256];
	va_list Args;
	va_start( Args, Fmt );
	vsnprintf( Buf, sizeof(Buf), Fmt, Args );
	va_end( Args );
	AmigaDebugLog( Buf );
}

static void AmigaRunStaticCtors()
{
	// libnix's crt (-noixemul) never runs C++ global constructors, so do it
	// here before anything else. The custom linker script (amiga68k.ld)
	// gathers the per-object .list___CTOR_LIST__ pointers into one contiguous
	// [start,end) array — each entry is that object's static-init function.
	// Run last-to-first per GCC convention; skip any null padding slots.
	for( CtorFn* p = __UE_CTOR_END; p > __UE_CTOR_START; )
	{
		CtorFn Fn = *--p;
		if( Fn )
		{
			Fn();
			++GAmigaCtorsRun;
		}
	}
}
#endif

#ifdef PLATFORM_AMIGA
// Real entry point; main() below wraps this on a large swapped stack.
static int UnrealMain( int argc, const char** argv )
#else
int main( int argc, const char** argv )
#endif
#endif
{
#ifdef PLATFORM_AMIGA
	// libnix's -noixemul crt does NOT zero the BSS segment, so zero-initialized
	// globals start with garbage. The intrusive singly-linked lists that some
	// of them head (the static-export list via GExportsTable; the autoregister
	// list via FObjectManager::AutoRegister) then close into cycles → infinite
	// loops at startup. Reset those list heads to NULL before the C++ global
	// constructors run and start prepending to them.
	// (A full BSS wipe here is NOT safe: libnix has already initialized its own
	// stdio/malloc state in BSS during its INIT_LIST, so zeroing everything
	// breaks fopen/malloc. Only clear the heads we know head a list.)
	{ extern FPackageExport* GExportsTable; GExportsTable = NULL; }
	FObjectManager::AmigaResetAutoRegister();
	AmigaResetIntrinsicRegistry();

	AmigaRunStaticCtors();
	{ char b[64]; snprintf(b,sizeof(b),"[Amiga] Ran %d global constructors.",GAmigaCtorsRun); AmigaDebugLog(b); }
#endif
#ifdef PLATFORM_WIN32
	hInstance = hInInstance;
#else
	hInstance = NULL;
	// Remember arguments since we don't have GetCommandLine().
	appSetCmdLine( argc, argv );
	PlatformPreInit();
#endif

	GIsStarted = 1;

	// Set package name.
	appStrcpy( THIS_PACKAGE, appPackage() );

	// Init mode.
	GIsServer = 1;
	GIsClient = !ParseParam(appCmdLine(),"SERVER") && !ParseParam(appCmdLine(),"MAKE");
	GIsEditor = ParseParam(appCmdLine(),"EDITOR") || ParseParam(appCmdLine(),"MAKE");

	// Init windowing.
	appChdir( appBaseDir() );

	// Init log.
	// TODO: GLog
	GExecHook = GThisExecHook;

	// Begin.
#ifndef _DEBUG
	try
	{
#endif
		// Start main loop.
		GIsGuarded=1;
		GSystem = &GTempPlatform;
		UEngine* Engine = InitEngine();
		if( !GIsRequestingExit )
			MainLoop( Engine );
		ExitEngine( Engine );
		GIsGuarded=0;
#ifndef _DEBUG
	}
	catch( ... )
	{
		// Crashed.
		try {HandleError();} catch( ... ) {}
	}
#endif

	// Shut down.
	GExecHook=NULL;
	appExit();
	GIsStarted = 0;

#ifdef PLATFORM_AMIGA
	// After we return, libnix runs the C++ global destructors, which tear down
	// the ~108 intrinsic UClass objects. UObject::~UObject then touches the
	// GObj.Objects table and asserts IsValid() while the table is mid-teardown,
	// crashing with "terminate". The desktop OSes just exit the process without
	// running these. Set GIsCriticalError so ~UObject skips all table access.
	GIsCriticalError = 1;
#endif
	return 0;
}

#ifdef PLATFORM_AMIGA
// AmigaOS Shell/WB give a tiny default stack, and libnix ignores __stack. UE's
// deep recursion (GC, serialization) overflows it and corrupts globals. Run the
// real entry point on a large exec-swapped stack. See AmigaStack.c.
extern "C" int AmigaRunWithStack( int argc, const char** argv, int (*fn)( int, const char** ) );
// libGL.a registers this through the stock linker's CONSTRUCTORS directive.
// Our custom ctor layout deliberately replaces that directive, so invoke the
// library initializer explicitly before any GL trampoline uses glBase.
extern "C" void INIT_8_OpenLibs();
extern "C" void EXIT_8_OpenLibs();
// libnix's low-level process exit.  Unlike exit(), this does not walk the
// stock C++ destructor list, which our custom constructor layout replaces.
extern "C" void AmigaLowLevelExit( int Result ) __asm__("____exit");
extern "C" void* glBase;

int main( int argc, const char** argv )
{
	{ FILE* F=fopen("PROGDIR:startup-debug.log","w"); if(F) fclose(F); }

	// One-shot: does C++ exception unwinding actually work under libnix/68k?
	// The engine relies on throw/catch (appError → main's catch). If this
	// prints "NOT CAUGHT" or crashes, exceptions are broken and that explains
	// the "terminate" at startup.
	{
		FILE* f = fopen( "PROGDIR:exc-test.log", "w" );
		volatile int stage = 0;
		try { stage = 1; throw (char*)"boom"; }
		catch( char* e ) { if(f) fprintf( f, "CAUGHT char* '%s' (stage=%d)\n", e, stage ); }
		catch( ... )     { if(f) fprintf( f, "CAUGHT ... (stage=%d)\n", stage ); }
		if(f){ fprintf(f,"after try, ok\n"); fclose(f); }
	}

	AmigaDebugLogf( "[Amiga] PreStack GL: glBase before init=%p", glBase );
	INIT_8_OpenLibs();
	AmigaDebugLogf( "[Amiga] PreStack GL: glBase after init=%p", glBase );

	// Create the SDL 1.2/AmigaMesa context on the original process stack.
	// UE itself still runs on the large swapped stack below.
	AmigaDebugLog( "[Amiga] PreStack GL: before SDL_Init" );
	if( SDL_Init( SDL_INIT_VIDEO ) == 0 )
	{
		SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		AmigaDebugLog( "[Amiga] PreStack GL: before SDL_CreateWindow" );
		SDL_Window* EarlyWindow = SDL_CreateWindow( "Unreal", SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED, 640, 480, SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN );
		AmigaDebugLogf( "[Amiga] PreStack GL: window=%p error='%s'", (void*)EarlyWindow, SDL_GetError() );
	}
	else
	{
		AmigaDebugLogf( "[Amiga] PreStack GL: SDL_Init failed: %s", SDL_GetError() );
	}
	const int Result = AmigaRunWithStack( argc, argv, &UnrealMain );
	AmigaDebugLog( "[Amiga] main: engine exited; closing GL libraries" );
	EXIT_8_OpenLibs();
	AmigaDebugLog( "[Amiga] main: clean low-level exit" );
	AmigaLowLevelExit( Result );
	return Result; // AmigaLowLevelExit does not return; keeps the compiler satisfied.
}
#endif
