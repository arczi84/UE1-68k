#include <stdlib.h>
#include <ctype.h>

#include "NSDLDrv.h"
#include "UnRender.h"
#ifdef AMIGA_USE_NATIVE_MINIGL
#include "AmigaMiniGLWindow.h"
#endif

#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
extern INT GAmigaStartupTraceFrames;
#if defined(NSDLDRV_USE_MINIGL) && !defined(AMIGA_USE_NATIVE_MINIGL)
extern "C" void AmigaCenterSDLWindow( void );
#endif
#endif

IMPLEMENT_CLASS( UNSDLClient );

/*-----------------------------------------------------------------------------
	UNSDLClient implementation.
-----------------------------------------------------------------------------*/

//
// Static init.
//
void UNSDLClient::InternalClassInitializer( UClass* Class )
{
	guard(UNSDLClient::InternalClassInitializer);
	if( appStricmp( Class->GetName(), "NSDLClient" ) == 0 )
	{
		new(Class, "DefaultDisplay",    RF_Public)UIntProperty(CPP_PROPERTY(DefaultDisplay),     "Display",  CPF_Config );
		new(Class, "StartupFullscreen", RF_Public)UBoolProperty(CPP_PROPERTY(StartupFullscreen), "Display",  CPF_Config );
		new(Class, "UseJoystick",       RF_Public)UBoolProperty(CPP_PROPERTY(UseJoystick),       "Joystick", CPF_Config );
		new(Class, "DeadZoneXYZ",       RF_Public)UFloatProperty(CPP_PROPERTY(DeadZoneXYZ),      "Joystick", CPF_Config );
		new(Class, "DeadZoneRUV",       RF_Public)UFloatProperty(CPP_PROPERTY(DeadZoneRUV),      "Joystick", CPF_Config );
		new(Class, "ScaleXYZ",          RF_Public)UFloatProperty(CPP_PROPERTY(ScaleXYZ),         "Joystick", CPF_Config );
		new(Class, "ScaleRUV",          RF_Public)UFloatProperty(CPP_PROPERTY(ScaleRUV),         "Joystick", CPF_Config );
		new(Class, "InvertY",           RF_Public)UBoolProperty(CPP_PROPERTY(InvertY),           "Joystick", CPF_Config );
		new(Class, "InvertV",           RF_Public)UBoolProperty(CPP_PROPERTY(InvertV),           "Joystick", CPF_Config );
	}
	unguard;
}

//
// UNSDLClient constructor.
//
UNSDLClient::UNSDLClient()
{
	guard(UNSDLClient::UWindowsClient);
	Controller = NULL;
	DefaultDisplay = 0;
	UseJoystick = true;
	ScaleXYZ = 100.f;
	ScaleRUV = 100.f;
	DeadZoneXYZ = 0.1f;
	DeadZoneRUV = 0.1f;
	unguard;
}

//
// Initialize the platform-specific viewport manager subsystem.
// Must be called after the Unreal object manager has been initialized.
// Must be called before any viewports are created.
//
void UNSDLClient::Init( UEngine* InEngine )
{
	guard(UNSDLClient::Init);

	// Init base.
	UClient::Init( InEngine );

#ifdef PLATFORM_AMIGA
	// Old Amiga configs could contain zero SDL2 scales.  A zero scale masks
	// every joystick axis regardless of the SDL 1.2 range.
	if( ScaleXYZ <= 0.f ) ScaleXYZ = 100.f;
	if( ScaleRUV <= 0.f ) ScaleRUV = 100.f;
#endif

	Controller = NULL;

	if ( SDL_Init( SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER ) < 0 )
	{
		appErrorf( "SDL_Init failed: %s", SDL_GetError() );
		appExit();
	}

	atexit( SDL_Quit );

#if defined(PLATFORM_AMIGA) && !defined(AMIGA_USE_NATIVE_MINIGL)
	// Create the selected renderer's window before loading the map. AmigaMesa
	// needs its large contiguous context allocation while memory is still
	// unfragmented; SoftDrv needs the same early window without SDL_OPENGL.
	char RenderClass[256] = "";
	// On Amiga the single executable uses GameRenderDevice for both windowed
	// and fullscreen modes. WindowedRenderDevice is a legacy fallback and
	// must not override the renderer selected in the in-game menu.
	GetConfigString( "Engine.Engine", "GameRenderDevice",
		RenderClass, ARRAY_COUNT(RenderClass) );
	appStrupr( RenderClass );
	const UBOOL WantsOpenGL = appStrstr( RenderClass, "OPENGL" ) != NULL
		|| appStrstr( RenderClass, "MINIGL" ) != NULL;
	if( WantsOpenGL )
	{
		SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
		SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
	}
	const INT EarlyX = ViewportX > 0 ? ViewportX : 640;
	const INT EarlyY = ViewportY > 0 ? ViewportY : 480;
	// Some real Picasso96 drivers (notably VideoCore) cannot reliably turn an
	// already-created OpenGL window into a public fullscreen screen.  SDL 1.2
	// ports which work on those drivers request SDL_FULLSCREEN in the initial
	// SDL_SetVideoMode call, so do the same when startup fullscreen is enabled.
	const Uint32 EarlyFlags = (WantsOpenGL ? SDL_WINDOW_OPENGL : 0)
		| (StartupFullscreen ? SDL_WINDOW_FULLSCREEN : 0)
		| SDL_WINDOW_HIDDEN;
	AmigaDebugLogf( "[Amiga] Client: creating early %s window %dx%d; startup fullscreen=%d",
		WantsOpenGL ? "OpenGL" : "Software", EarlyX, EarlyY, (INT)StartupFullscreen );
	SDL_Window* EarlyWindow = SDL_CreateWindow( "Unreal", SDL_WINDOWPOS_UNDEFINED,
		SDL_WINDOWPOS_UNDEFINED, EarlyX, EarlyY, EarlyFlags );
#ifdef NSDLDRV_USE_MINIGL
	// SDL 1.2 hands this layer the already-visible pre-stack window. Reassert
	// its position after the client has adopted it and updated its caption.
	if( EarlyWindow && !StartupFullscreen )
		AmigaCenterSDLWindow();
#endif
	AmigaDebugLogf( "[Amiga] Client: early window=%p error='%s'", (void*)EarlyWindow, SDL_GetError() );
	if( !EarlyWindow )
		appErrorf( "Could not create early Amiga window: %s", SDL_GetError() );
	// SDL 1.2 exposes the window immediately from SDL_SetVideoMode, even when
	// the SDL2 compatibility flags say HIDDEN. Grab it now so startup does not
	// leave the Workbench pointer visible over the game while the map loads.
	if( CaptureMouse )
		SDL_SetRelativeMouseMode( SDL_TRUE );
#endif

	if( SDL_NumJoysticks() > 0 )
		Controller = SDL_GameControllerOpen( 0 );

	SDL_GameControllerEventState( SDL_ENABLE );

	// SDL 1.2 only fills keysym.unicode after SDL_EnableUNICODE(), exposed by
	// our compatibility layer as SDL_StartTextInput(). Without it the console
	// opens, but receives no printable characters.
#ifdef PLATFORM_SDL12_COMPAT
	SDL_StartTextInput();
#else
	// Not calling SDL_StartTextInput because that pops up on-screen keyboards sometimes.
	SDL_EventState( SDL_TEXTINPUT, SDL_ENABLE );
#endif

	SDL_GetDesktopDisplayMode( DefaultDisplay, &DefaultDisplayMode );

	unguard;
}

//
// Shut down the platform-specific viewport manager subsystem.
//
void UNSDLClient::Destroy()
{
	guard(UNSDLClient::Destroy);

#ifdef AMIGA_USE_NATIVE_MINIGL
	// GC can destroy the client before its viewports. Their renderers and
	// native windows must be shut down while SDL's OS library bases are alive.
	// ConditionalDestroy marks them for GC, preventing a second Destroy call;
	// viewport destruction removes the entry from Viewports, hence reverse order.
	for( INT i=Viewports.Num()-1; i>=0; --i )
		Viewports(i)->ConditionalDestroy();
#endif

	if (Controller)
	{
		SDL_GameControllerClose(Controller);
		Controller = NULL;
	}

	SDL_QuitSubSystem( SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER );

	UClient::Destroy();

	unguard;
}

//
// Failsafe routine to shut down viewport manager subsystem
// after an error has occured. Not guarded.
//
void UNSDLClient::ShutdownAfterError()
{
	debugf( NAME_Exit, "Executing UNSDLClient::ShutdownAfterError" );

	if( Engine && Engine->Audio )
	{
		Engine->Audio->ConditionalShutdownAfterError();
	}

	for( INT i=Viewports.Num()-1; i>=0; i-- )
	{
		UNSDLViewport* Viewport = (UNSDLViewport*)Viewports(i);
	}

	Super::ShutdownAfterError();
}

//
// Enable or disable all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void UNSDLClient::EnableViewportWindows( DWORD ShowFlags, int DoEnable )
{
	guard(UNSDLClient::EnableViewportWindows);
	unguard;
}

//
// Show or hide all viewport windows that have ShowFlags set (or all if ShowFlags=0).
//
void UNSDLClient::ShowViewportWindows( DWORD ShowFlags, int DoShow )
{
	guard(UNSDLClient::ShowViewportWindows);
	unguard;
}

//
// Configuration change.
//
void UNSDLClient::PostEditChange()
{
	guard(UNSDLClient::PostEditChange);
	Super::PostEditChange();
	unguard;
}

// Perform background processing.  Should be called 100 times
// per second or more for best results.
//
void UNSDLClient::Poll()
{
	guard(UNSDLClient::Poll);
	unguard;
}

//
// Perform timer-tick processing on all visible viewports.  This causes
// all realtime viewports, and all non-realtime viewports which have been
// updated, to be blitted.
//
void UNSDLClient::Tick()
{
	guard(UNSDLClient::Tick);
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: ClientTick enter viewports=%d", Viewports.Num() );
		AmigaDebugLogf( "[Amiga] AUTO FIRST ClientTick enter viewports=%d", Viewports.Num() );
	}
#endif

	// Process input and blit any viewports that need blitting.
	UNSDLViewport* BestViewport = NULL;
	for( INT i=0; i<Viewports.Num(); i++ )
	{
		UNSDLViewport* Viewport = CastChecked<UNSDLViewport>(Viewports(i));
		if( !Viewport->GetWindow() )
		{
			// Window was closed via close button.
			delete Viewport;
			return;
		}
		else if (	Viewport->IsRealtime()
			&&	(Viewport==FullscreenViewport || FullscreenViewport==NULL)
			&&	Viewport->SizeX && Viewport->SizeY && !Viewport->OnHold
			&&	(!BestViewport || Viewport->LastUpdateTime<BestViewport->LastUpdateTime) )
		{
			BestViewport = Viewport;
		}
		// Tick input for this viewport and see if it wants to die.
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
		{
			debugf( NAME_Init, "FIRST: ClientTick before input viewport=%d", i );
			AmigaDebugLogf( "[Amiga] AUTO FIRST ClientTick before input viewport=%d", i );
		}
#endif
		if( Viewport->TickInput() )
		{
			delete Viewport;
			return;
		}
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
		{
			debugf( NAME_Init, "FIRST: ClientTick after input viewport=%d", i );
			AmigaDebugLogf( "[Amiga] AUTO FIRST ClientTick after input viewport=%d", i );
		}
#endif
	}

	if( BestViewport )
	{
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
		{
			debugf( NAME_Init, "FIRST: ClientTick before repaint" );
			AmigaDebugLogf( "[Amiga] AUTO FIRST ClientTick before repaint" );
		}
#endif
		BestViewport->Repaint();
	}
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: ClientTick after repaint" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST ClientTick after repaint" );
	}
#endif

	unguard;
}

//
// Create a new viewport.
//
UViewport* UNSDLClient::NewViewport( class ULevel* InLevel, const FName Name )
{
	guard(UNSDLClient::NewViewport);
	return new( GObj.GetTransientPackage(), Name )UNSDLViewport( InLevel, this );
	unguard;
}

//
// Return the current viewport.  Returns NULL if no viewport has focus.
//
UViewport* UNSDLClient::CurrentViewport()
{
	guard(UNSDLClient::CurrentViewport);

	if( FullscreenViewport )
		return FullscreenViewport;

#ifdef AMIGA_USE_NATIVE_MINIGL
	if( AmigaMiniGLHasFocus() )
	{
		for( int i=0; i<Viewports.Num(); i++ )
		{
			UNSDLViewport* Viewport = (UNSDLViewport*)Viewports(i);
			if( Viewport->GetWindow() == AmigaMiniGLGetWindow() )
				return Viewport;
		}
	}
#endif

	SDL_Window *MouseWin = SDL_GetMouseFocus();
	UNSDLViewport* TestViewport = NULL;
	for( int i=0; i<Viewports.Num(); i++ )
	{
		TestViewport = (UNSDLViewport*)Viewports(i);
		if( TestViewport->Current || (SDL_Window*)TestViewport->GetWindow() == MouseWin )
			return TestViewport;
	}

	return NULL;

	unguard;
}

//
// Assemble display mode array.
//
static INT Compare( const SDL_Rect& A, const SDL_Rect& B )
{
	if( A.w == B.w )
		return A.h - B.h;
	return A.w - B.w;
}
static UBOOL operator==( const SDL_Rect& A, const SDL_Rect& B )
{
	return A.w == B.w && A.h == B.h;
}
const TArray<SDL_Rect>& UNSDLClient::GetDisplayResolutions()
{
	guard(UNSDLClient::GetDisplayResolutions)

	DisplayResolutions.Empty();

	SDL_DisplayMode Mode;
	SDL_Rect Rect = { 0, 0, 0, 0 };
	if( SDL_GetDesktopDisplayMode( DefaultDisplay, &Mode ) == 0 )
	{
		// we're only interested in size
		Rect.w = Mode.w;
		Rect.h = Mode.h;
		DisplayResolutions.AddItem( Rect );
	}

	const INT NumModes = SDL_GetNumDisplayModes( DefaultDisplay );
	
	for( INT i=0; i<NumModes; ++i )
	{
		if( SDL_GetDisplayMode( DefaultDisplay, i, &Mode ) == 0 )
		{
			// we're only interested in size
			Rect.w = Mode.w;
			Rect.h = Mode.h;
			DisplayResolutions.AddUniqueItem( Rect );
		}
	}

	appSort( &DisplayResolutions(0), DisplayResolutions.Num() );

	return DisplayResolutions;

	unguard;
}

//
// Command line.
//
UBOOL UNSDLClient::Exec( const char* Cmd, FOutputDevice* Out )
{
	guard(UNSDLClient::Exec);

	if( ParseCommand( &Cmd, "EndFullscreen" ) )
	{
		EndFullscreen();
		return 1;
	}
	else if( ParseCommand( &Cmd, "GetRes" ) )
	{
		FString Result;
		GetDisplayResolutions();
		for( INT i=0; i<DisplayResolutions.Num(); ++i )
		{
			const SDL_Rect& Mode = DisplayResolutions(i);
			Result.Appendf( "%ix%i ", Mode.w, Mode.h );
		}
		Out->Log( *Result );
		return 1;
	}
	else if( UClient::Exec( Cmd, Out ) )
	{
		return 1;
	}

	return 0;

	unguard;
}

void UNSDLClient::EndFullscreen()
{
	UNSDLViewport* Viewport = Cast<UNSDLViewport>(FullscreenViewport);
	if( Viewport )
	{
		Viewport->EndFullscreen();
	}
	FullscreenViewport = NULL;
}

//
// Try switching to a new rendering device.
//
void UNSDLClient::TryRenderDevice( UViewport* Viewport, const char* ClassName, UBOOL Fullscreen )
{
	guard(UNSDLClient::TryRenderDevice);
	const char* LoadClassName = ClassName;
#ifdef NSDLDRV_USE_MINIGL
	// This executable contains MiniGL, not AmigaMesa. Map a hardware choice
	// from either compatible INI onto the MiniGL class without touching SoftDrv.
	char ConfiguredClass[256] = "";
	if( !appStricmp(ClassName, "ini:Engine.Engine.GameRenderDevice") )
		GetConfigString( "Engine.Engine", "GameRenderDevice",
			ConfiguredClass, ARRAY_COUNT(ConfiguredClass) );
	else if( !appStricmp(ClassName, "ini:Engine.Engine.WindowedRenderDevice") )
		GetConfigString( "Engine.Engine", "WindowedRenderDevice",
			ConfiguredClass, ARRAY_COUNT(ConfiguredClass) );
	if( ConfiguredClass[0] )
	{
		appStrupr( ConfiguredClass );
		if( appStrstr(ConfiguredClass, "OPENGL") || appStrstr(ConfiguredClass, "MINIGL") )
			LoadClassName = "NMiniGLDrv.NMiniGLRenderDevice";
	}
#endif

	// Recreating the renderer for a fullscreen toggle does not change the
	// viewport. Detaching audio here stops and unregisters the current module,
	// but the level has no new MusicEvent to start it again afterwards.
	const UBOOL HadRenderDevice = Viewport->RenDev != NULL;

	// Shut down current rendering device.
	if( Viewport->RenDev )
	{
		Viewport->RenDev->Exit();
		delete Viewport->RenDev;
		Viewport->RenDev = NULL;
	}

	// Find device driver.
	UClass* RenderClass = GObj.LoadClass( URenderDevice::StaticClass, NULL, LoadClassName, NULL, LOAD_KeepImports, NULL );
	if( RenderClass )
	{
		Viewport->RenDev = ConstructClassObject<URenderDevice>( RenderClass );
		if( !HadRenderDevice && Viewport->Client->Engine->Audio && !GIsEditor )
			Viewport->Client->Engine->Audio->SetViewport( NULL );
		if( Viewport->RenDev->Init( Viewport ) )
		{
			Viewport->Actor->XLevel->DetailChange( Viewport->RenDev->HighDetailActors );
			if( Fullscreen && !Viewport->Client->FullscreenViewport )
				Viewport->MakeFullscreen( Viewport->Client->ViewportX, Viewport->Client->ViewportY, 1 );
		}
		else
		{
			debugf( NAME_Log, LocalizeError("Failed3D") );
			delete Viewport->RenDev;
			Viewport->RenDev = NULL;
		}
		if( !HadRenderDevice && Viewport->Client->Engine->Audio && !GIsEditor )
			Viewport->Client->Engine->Audio->SetViewport( Viewport );
	}

	unguard;
}
