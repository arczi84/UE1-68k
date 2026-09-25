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
#ifdef AMIGA_USE_MINIGL
extern "C" void AmigaCenterSDLWindow( void );
#define AMIGA_UNREAL_INI "PROGDIR:Unreal-MiniGL.ini"
#define AMIGA_UNREAL_INI_ARG "-INI=Unreal-MiniGL.ini"
#else
#define AMIGA_UNREAL_INI "PROGDIR:Unreal.ini"
#define AMIGA_UNREAL_INI_ARG "-INI=Unreal.ini"
#endif
#ifdef UE_AMIGA_GPROF
#include <unistd.h>
// libnix does not select gcrt0.o for -pg, so its profiler must be started and
// flushed explicitly.  The normal Amiga build remains completely unaffected.
extern "C" void _monstartup();
extern "C" void _moncleanup();
#endif
// Shared with realtime/procedural textures, whose legacy DOUBLE->FLOAT timing
// path is unreliable with this 68k soft-float runtime.
FLOAT GAmigaFrameDeltaSeconds = 1.0f / 30.0f;
DWORD GAmigaFrameSerial = 1;
INT GAmigaStartupTraceFrames = 0;
INT GAmigaMiniGLTraceFrames = 0;
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
	debugf( NAME_Init, "FIRST: Engine->Init returned" );
	AmigaDebugLogf( "[Amiga] AUTO FIRST Engine->Init returned" );

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
	debugf( NAME_Init, "FIRST: MainLoop before initial SDL_GetTicks" );
	AmigaDebugLogf( "[Amiga] AUTO FIRST MainLoop before initial SDL_GetTicks" );
	DWORD AmigaOldTicks = SDL_GetTicks();
	debugf( NAME_Init, "FIRST: MainLoop initial ticks=%lu", (unsigned long)AmigaOldTicks );
	AmigaDebugLogf( "[Amiga] AUTO FIRST MainLoop initial ticks=%lu", (unsigned long)AmigaOldTicks );
	DWORD AmigaFrameStart = AmigaOldTicks;
	DWORD AmigaEngineMillis = 0;
	INT AmigaFrameCount = 0;
	// Isolation modes are meant to run at full speed.  The earlier per-frame
	// console/file trace distorted timing badly on a real 68k machine.
	GAmigaStartupTraceFrames = 0;
	GAmigaMiniGLTraceFrames = 0;
#else
	DOUBLE OldTime = appSeconds();
#endif
	while( GIsRunning && !GIsRequestingExit )
	{
		// Update the world.
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
			AmigaDebugLogf( "[Amiga] AUTO TRACE frame=%lu phase=loop-start remaining=%d", (unsigned long)(GAmigaFrameSerial+1), GAmigaStartupTraceFrames );
		const DWORD AmigaNewTicks = SDL_GetTicks();
		const DWORD AmigaDeltaMillis = AmigaNewTicks - AmigaOldTicks;
		AmigaOldTicks = AmigaNewTicks;
		AmigaEngineMillis += AmigaDeltaMillis;
		// Do not feed a multi-second disk/audio stall back into simulation.
		// Besides exploding walking-physics substeps, an unbounded delta makes
		// Nyleve's animated sky panners jump far enough to collapse into bands.
		// Normal frames are unchanged; only gaps above 100 ms are clamped.
		GAmigaFrameDeltaSeconds = (FLOAT)Min<DWORD>(AmigaDeltaMillis, 100) * 0.001f;
		++GAmigaFrameSerial;
		if( GAmigaStartupTraceFrames > 0 )
		{
			debugf( NAME_Init, "FIRST: MainLoop before Engine->Tick delta_ms=%lu", (unsigned long)AmigaDeltaMillis );
			AmigaDebugLogf( "[Amiga] AUTO FIRST MainLoop before Engine->Tick delta_ms=%lu", (unsigned long)AmigaDeltaMillis );
		}
		Engine->Tick( GAmigaFrameDeltaSeconds );
		if( GAmigaStartupTraceFrames > 0 )
		{
			debugf( NAME_Init, "FIRST: MainLoop after Engine->Tick" );
			AmigaDebugLogf( "[Amiga] AUTO FIRST MainLoop after Engine->Tick" );
		}
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
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
			AmigaDebugLogf( "[Amiga] AUTO TRACE frame=%lu phase=before-max-tick-rate", (unsigned long)GAmigaFrameSerial );
#endif
		INT MaxTickRate = Engine->GetMaxTickRate();
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
			AmigaDebugLogf( "[Amiga] AUTO TRACE frame=%lu phase=after-max-tick-rate value=%d", (unsigned long)GAmigaFrameSerial, MaxTickRate );
#endif
		if( MaxTickRate )
		{
#ifdef PLATFORM_AMIGA
			const DWORD TargetMillis = 1000 / MaxTickRate;
			const DWORD UsedMillis = SDL_GetTicks() - AmigaOldTicks;
			if( UsedMillis < TargetMillis )
			{
				if( GAmigaStartupTraceFrames > 0 )
					AmigaDebugLogf( "[Amiga] AUTO TRACE frame=%lu phase=before-delay ms=%lu", (unsigned long)GAmigaFrameSerial, (unsigned long)(TargetMillis-UsedMillis) );
				SDL_Delay( TargetMillis - UsedMillis );
				if( GAmigaStartupTraceFrames > 0 )
					AmigaDebugLogf( "[Amiga] AUTO TRACE frame=%lu phase=after-delay", (unsigned long)GAmigaFrameSerial );
			}
#else
			DOUBLE Delta = (1.0/MaxTickRate) - (appSeconds()-OldTime);
			if( Delta > 0.0 )
				appSleep( Delta );
#endif
		}
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
		{
			AmigaDebugLogf( "[Amiga] AUTO TRACE frame=%lu phase=loop-end", (unsigned long)GAmigaFrameSerial );
			--GAmigaStartupTraceFrames;
		}
#endif
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

#ifdef PLATFORM_AMIGA
	// The low-level exit used below skips FConfigCache's global destructor.
	// Flush dirty .ini files while the object/config systems are still alive.
	SaveAllConfigs();
#endif
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
// Keep the normal 68k build quiet. Opening and closing this file for every
// package, texture and input event makes map startup needlessly expensive on
// an emulated Amiga disk. Only retain failures; automatic debug traces are
// disabled for the interactive MiniGL tests.
extern "C" void AmigaDebugLog( const char* Msg )
{
#ifdef UE_RELEASE_PACKAGE
	(void)Msg;
#else
	if
	(	!strstr(Msg,"FAIL")
	&&	!strstr(Msg,"Error")
	&&	!strstr(Msg,"ERROR")
	&&	!strstr(Msg,"Critical")
	&&	!strstr(Msg,"threw")
	&&	!strstr(Msg,"crash") )
		return;

	// Commit every diagnostic line to persistent storage immediately. Keeping
	// the stream open loses the directory update when PiStorm locks the whole
	// machine and the user must reset it.
	FILE* F = fopen( "PROGDIR:unreal-first.log", "a" );
	if( !F )
		F = fopen( "unreal-first.log", "a" );
	if( !F )
		F = fopen( "SYS:unreal-first.log", "a" );
	if( F )
	{
		fputs( Msg, F );
		fputc( '\n', F );
		fflush( F );
		fclose( F );
	}
	fputs( Msg, stderr );
	fputc( '\n', stderr );
	fflush( stderr );
#endif
}

// printf-style variant used by the startup traces in other TUs.
#include <stdarg.h>
extern "C" void AmigaDebugLogf( const char* Fmt, ... )
{
#ifdef UE_RELEASE_PACKAGE
	(void)Fmt;
#else
	char Buf[256];
	va_list Args;
	va_start( Args, Fmt );
	vsnprintf( Buf, sizeof(Buf), Fmt, Args );
	va_end( Args );
	AmigaDebugLog( Buf );
#endif
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
#ifdef PLATFORM_AMIGA
	// Use one canonical configuration name regardless of whether Workbench
	// launches Unreal or the Unreal-hard compatibility alias.
	const char* IniArgv[64];
	IniArgv[0] = argc>0 ? argv[0] : "Unreal";
	INT IniArgc = 1;
	for( INT i=1; i<argc && IniArgc<(INT)ARRAY_COUNT(IniArgv)-1; ++i )
		IniArgv[IniArgc++] = argv[i];
	IniArgv[IniArgc++] = AMIGA_UNREAL_INI_ARG;
	appSetCmdLine( IniArgc, IniArgv );
#else
	appSetCmdLine( argc, argv );
#endif
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
			debugf( NAME_Init, "FIRST: InitEngine returned requesting_exit=%d", (INT)GIsRequestingExit );
			AmigaDebugLogf( "[Amiga] AUTO FIRST InitEngine returned requesting_exit=%d", (INT)GIsRequestingExit );
			if( !GIsRequestingExit )
			{
				debugf( NAME_Init, "FIRST: before MainLoop" );
				AmigaDebugLogf( "[Amiga] AUTO FIRST before MainLoop" );
				MainLoop( Engine );
			}
#ifdef UE_AMIGA_GPROF
		_moncleanup();
#endif
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
#ifdef UE_AMIGA_GPROF
	// Save before UObject/renderer destruction, not only after it returns.
	_moncleanup();
#endif
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
#ifndef AMIGA_USE_MINIGL
// libGL.a registers this through the stock linker's CONSTRUCTORS directive.
// Our custom ctor layout deliberately replaces that directive, so invoke the
// library initializer explicitly before any GL trampoline uses glBase.
extern "C" void INIT_8_OpenLibs();
extern "C" void EXIT_8_OpenLibs();
#endif
// libnix's low-level process exit.  Unlike exit(), this does not walk the
// stock C++ destructor list, which our custom constructor layout replaces.
extern "C" void AmigaLowLevelExit( int Result ) __asm__("____exit");
#ifndef AMIGA_USE_MINIGL
extern "C" void* glBase;
#endif

// The SDL 1.2 backend has no hidden-window support: SDL_WINDOW_HIDDEN is
// discarded by the compatibility shim.  The pre-stack AmigaMesa context must
// therefore be created at the saved viewport size immediately.  Creating it
// at 640x480 and shrinking it later leaves the old 640x480 drawable visible
// outside a 320x240 Intuition window until Workbench happens to repaint it.
static void AmigaReadEarlyViewportSize( int& Width, int& Height )
{
	Width = 640;
	Height = 480;

	FILE* Ini = fopen( AMIGA_UNREAL_INI, "r" );
	if( !Ini )
		return;

	char Line[256];
	int InSDLClient = 0;
	while( fgets( Line, sizeof(Line), Ini ) )
	{
		if( Line[0] == '[' )
		{
			InSDLClient = strncmp( Line, "[NSDLDrv.NSDLClient]", 20 ) == 0;
			continue;
		}
		if( InSDLClient )
		{
			int Value;
			if( sscanf( Line, "ViewportX=%d", &Value ) == 1 && Value >= 160 && Value <= 4096 )
				Width = Value;
			else if( sscanf( Line, "ViewportY=%d", &Value ) == 1 && Value >= 120 && Value <= 4096 )
				Height = Value;
		}
	}
	fclose( Ini );

	// Match UNSDLViewport::OpenWindow's horizontal alignment.
	Width = (Width + 3) & ~3;
}

// Read the renderer before the UE object/config systems exist. The launcher
// must know whether its early SDL 1.2 surface may use SDL_OPENGL: a GL surface
// has no writable pixels and therefore cannot be reused by SoftDrv.
static int AmigaReadEarlyUseOpenGL()
{
	int UseOpenGL = 1;
	FILE* Ini = fopen( AMIGA_UNREAL_INI, "r" );
	if( !Ini )
		return UseOpenGL;

	char Line[256];
	int InEngine = 0;
	while( fgets( Line, sizeof(Line), Ini ) )
	{
		if( Line[0] == '[' )
		{
			InEngine = strncmp( Line, "[Engine.Engine]", 15 ) == 0;
			continue;
		}
		if( InEngine && strncmp( Line, "GameRenderDevice=", 17 ) == 0 )
		{
			UseOpenGL = strstr( Line + 17, "SoftDrv.SoftwareRenderDevice" ) == NULL;
			break;
		}
	}
	fclose( Ini );
	return UseOpenGL;
}

// VideoCore's SDL/CyberGraphics backend can open a fullscreen screen when
// SDL_FULLSCREEN is present in the first SDL_SetVideoMode call, but may fall
// back to a Workbench window when an existing window is switched afterwards.
// Read this setting before the UE config system exists so the pre-stack video
// surface is created in the requested mode immediately.
static int AmigaReadEarlyFullscreen()
{
	int Fullscreen = 0;
	FILE* Ini = fopen( AMIGA_UNREAL_INI, "r" );
	if( !Ini )
		return Fullscreen;

	char Line[256];
	int InClient = 0;
	while( fgets( Line, sizeof(Line), Ini ) )
	{
		if( Line[0] == '[' )
		{
			InClient = strncmp( Line, "[NSDLDrv.NSDLClient]", 20 ) == 0;
			continue;
		}
		if( InClient && strncmp( Line, "StartupFullscreen=", 18 ) == 0 )
		{
			const char* Value = Line + 18;
			Fullscreen = atoi( Value ) != 0
				|| strncmp( Value, "True", 4 ) == 0
				|| strncmp( Value, "true", 4 ) == 0;
			break;
		}
	}
	fclose( Ini );
	return Fullscreen;
}

// AmigaStack.c also needs this when deciding whether it may close the GL
// libraries after the swapped-stack engine run.
extern "C" int GAmigaUseOpenGL = 1;

int main( int argc, const char** argv )
{
#ifdef UE_AMIGA_GPROF
	// main's own injected mcount runs before this call while profiling is
	// disabled.  Start collection before any engine or SDL work begins.
	_monstartup();
#endif

#ifndef UE_RELEASE_PACKAGE
	{ FILE* F=fopen("PROGDIR:startup-debug.log","w"); if(F) fclose(F); }
	{ FILE* F=fopen("PROGDIR:unreal-first.log","w"); if(F) fclose(F); }

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
#endif

#ifndef AMIGA_USE_NATIVE_MINIGL
	GAmigaUseOpenGL = AmigaReadEarlyUseOpenGL();
#ifndef AMIGA_USE_MINIGL
	if( GAmigaUseOpenGL )
	{
		AmigaDebugLogf( "[Amiga] PreStack GL: glBase before init=%p", glBase );
		INIT_8_OpenLibs();
		AmigaDebugLogf( "[Amiga] PreStack GL: glBase after init=%p", glBase );
	}
#endif

	// Create the selected SDL surface on the original process stack. OpenGL
	// gets its AmigaMesa context early; Software gets a writable RGB565 surface.
	// UE itself still runs on the large swapped stack below.
	AmigaDebugLog( "[Amiga] PreStack: before SDL_Init" );
	if( SDL_Init( SDL_INIT_VIDEO ) == 0 )
	{
		if( GAmigaUseOpenGL )
		{
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
		}
		int EarlyWidth, EarlyHeight;
		AmigaReadEarlyViewportSize( EarlyWidth, EarlyHeight );
		const int EarlyFullscreen = AmigaReadEarlyFullscreen();
		AmigaDebugLog( "[Amiga] PreStack: before SDL_CreateWindow" );
		SDL_Window* EarlyWindow = SDL_CreateWindow( "Unreal", SDL_WINDOWPOS_UNDEFINED,
			SDL_WINDOWPOS_UNDEFINED, EarlyWidth, EarlyHeight,
			(GAmigaUseOpenGL ? SDL_WINDOW_OPENGL : 0)
				| (EarlyFullscreen ? SDL_WINDOW_FULLSCREEN : 0)
				| SDL_WINDOW_HIDDEN );
#ifdef AMIGA_USE_MINIGL
		if( EarlyWindow && !EarlyFullscreen )
			AmigaCenterSDLWindow();
#endif
		AmigaDebugLogf( "[Amiga] PreStack: renderer=%s fullscreen=%d window=%p error='%s'",
			GAmigaUseOpenGL ?
#ifdef AMIGA_USE_MINIGL
				"MiniGL"
#else
				"OpenGL"
#endif
				: "Software", EarlyFullscreen,
			(void*)EarlyWindow, SDL_GetError() );
	}
	else
	{
		AmigaDebugLogf( "[Amiga] PreStack: SDL_Init failed: %s", SDL_GetError() );
	}
#else
	AmigaDebugLog( "[Amiga] Native MiniGL: SDL window/context bootstrap bypassed" );
#endif
	const int Result = AmigaRunWithStack( argc, argv, &UnrealMain );
#ifndef AMIGA_USE_MINIGL
	if( GAmigaUseOpenGL )
	{
		AmigaDebugLog( "[Amiga] main: engine exited; closing GL libraries" );
		EXIT_8_OpenLibs();
	}
#endif
#ifdef UE_AMIGA_GPROF
	// AmigaLowLevelExit deliberately bypasses exit()/atexit(), so flush here.
	// Make the otherwise relative "gmon.out" name deterministic.
	AmigaDebugLog( "[Amiga] gprof: writing PROGDIR:gmon.out" );
	if( chdir( "PROGDIR:" ) != 0 )
		AmigaDebugLog( "[Amiga] gprof: could not chdir to PROGDIR:" );
	_moncleanup();
#endif
	AmigaDebugLog( "[Amiga] main: clean low-level exit" );
	AmigaLowLevelExit( Result );
	return Result; // AmigaLowLevelExit does not return; keeps the compiler satisfied.
}
#endif
