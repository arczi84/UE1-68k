#include <string.h>
#include <ctype.h>

#include "NSDLDrv.h"
#include "UnRender.h"
#ifdef AMIGA_USE_NATIVE_MINIGL
#include "AmigaMiniGLWindow.h"
#endif

#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
extern INT GAmigaStartupTraceFrames;
extern INT GAmigaMiniGLTraceFrames;
extern DWORD GAmigaFrameSerial;
#ifdef AMIGA_USE_NATIVE_MINIGL
// Log the first frames after a MiniGL mode switch (diagnoses PiStorm3D hangs).
static INT GAmigaModeSwitchTraceFrames = 0;
#endif
extern "C" int AmigaRawMouseSetCapture( int Enabled );
extern "C" int AmigaRawMouseRead( int* DX, int* DY, unsigned int* Pressed,
	unsigned int* Released, unsigned int* Held );
#if defined(NSDLDRV_USE_MINIGL) && !defined(AMIGA_USE_NATIVE_MINIGL)
extern "C" void AmigaCenterSDLWindow( void );
#endif
#define AMIGA_VIEW_LOG(...) AmigaDebugLogf( "[Amiga] Viewport: " __VA_ARGS__ )
#else
#define AMIGA_VIEW_LOG(...)
#endif

IMPLEMENT_CLASS( UNSDLViewport );

#ifdef AMIGA_USE_NATIVE_MINIGL
static INT OpenNativeMiniGLWindow( INT Width, INT Height, UBOOL Fullscreen )
{
	INT ColorBits = 16; // Preserve the previous hard-coded request.
	Parse( appCmdLine(), "MGLBPP=", ColorBits );
	if( ColorBits!=16 && ColorBits!=32 )
		appErrorf( "Invalid -mglbpp=%i: use 16 or 32", ColorBits );
	debugf( NAME_Log, "MiniGL: requested color depth=%i, fullscreen=%i (actual format is backend-dependent)",
		ColorBits, (INT)Fullscreen );
	return AmigaMiniGLOpenWindow( Width, Height, Fullscreen ? 1 : 0, ColorBits );
}
#endif

/*-----------------------------------------------------------------------------
	UNSDLViewport implementation.
-----------------------------------------------------------------------------*/

//
// SDL_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::MouseButtonMap[6] =
{
	/* invalid           */ IK_None,
	/* SDL_BUTTON_LEFT   */ IK_LeftMouse,
	/* SDL_BUTTON_MIDDLE */ IK_MiddleMouse,
	/* SDL_BUTTON_RIGHT  */ IK_RightMouse,
	/* SDL_BUTTON_X1     */ IK_None,
	/* SDL_BUTTON_X2     */ IK_None
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::JoyButtonMap[SDL_CONTROLLER_BUTTON_MAX] =
{
	/* BUTTON_A             */ IK_Joy1,
	/* BUTTON_B             */ IK_Joy2,
	/* BUTTON_X             */ IK_Joy3,
	/* BUTTON_Y             */ IK_Joy4,
	/* BUTTON_BACK          */ IK_Joy5,
	/* BUTTON_GUIDE         */ IK_Joy6,
	/* BUTTON_START         */ IK_Joy7,
	/* BUTTON_LEFTSTICK     */ IK_Joy8,
	/* BUTTON_RIGHTSTICK    */ IK_Joy9,
	/* BUTTON_LEFTSHOULDER  */ IK_Joy10,
	/* BUTTON_RIGHTSHOULDER */ IK_Joy11,
	/* BUTTON_DPAD_UP       */ IK_JoyPovUp,
	/* BUTTON_DPAD_DOWN     */ IK_JoyPovDown,
	/* BUTTON_DPAD_LEFT     */ IK_JoyPovLeft,
	/* BUTTON_DPAD_RIGHT    */ IK_JoyPovRight,
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map for UI controls.
//
const BYTE UNSDLViewport::JoyButtonMapUI[SDL_CONTROLLER_BUTTON_MAX] =
{
	/* BUTTON_A             */ IK_Enter,
	/* BUTTON_B             */ IK_Escape,
	/* BUTTON_X             */ IK_N,
	/* BUTTON_Y             */ IK_Y,
	/* BUTTON_BACK          */ IK_Escape,
	/* BUTTON_GUIDE         */ IK_Escape,
	/* BUTTON_START         */ IK_Escape,
	/* BUTTON_LEFTSTICK     */ IK_Joy8,
	/* BUTTON_RIGHTSTICK    */ IK_Joy9,
	/* BUTTON_LEFTSHOULDER  */ IK_Joy10,
	/* BUTTON_RIGHTSHOULDER */ IK_Joy11,
	/* BUTTON_DPAD_UP       */ IK_Up,
	/* BUTTON_DPAD_DOWN     */ IK_Down,
	/* BUTTON_DPAD_LEFT     */ IK_Left,
	/* BUTTON_DPAD_RIGHT    */ IK_Right,
};

//
// SDL_CONTROLLER_BUTTON_ -> EInputKey translation map.
//
const BYTE UNSDLViewport::JoyAxisMap[SDL_CONTROLLER_AXIS_MAX] =
{
	/* AXIS_LEFT_X          */ IK_JoyX,
	/* AXIS_LEFT_Y          */ IK_JoyY,
	/* AXIS_RIGHT_X         */ IK_JoyU,
	/* AXIS_RIGHT_Y         */ IK_JoyV,
	/* AXIS_LTRIGGER        */ IK_Joy12,
	/* AXIS_RTRIGGER        */ IK_Joy13,
};

//
// Additional scale to apply per SDL axis.
//
const FLOAT UNSDLViewport::JoyAxisDefaultScale[SDL_CONTROLLER_AXIS_MAX] =
{
	/* AXIS_LEFT_X          */ +60.f,
	/* AXIS_LEFT_Y          */ -60.f,
	/* AXIS_RIGHT_X         */ +60.f,
	/* AXIS_RIGHT_Y         */ +60.f,
	/* AXIS_LTRIGGER        */ +60.f,
	/* AXIS_RTRIGGER        */ +60.f,
};

//
// SDL_Scancode -> EInputKey translation map.
//
BYTE UNSDLViewport::KeyMap[SDL_NUM_SCANCODES];
void UNSDLViewport::InitKeyMap()
{
	#define INIT_KEY_RANGE( AStart, AEnd, BStart, BEnd ) \
		for( DWORD Key = AStart; Key <= AEnd; ++Key ) KeyMap[Key] = BStart + ( Key - AStart )

	appMemset( KeyMap, 0, sizeof( KeyMap ) );

	// TODO: IK_LControl, IK_LShift, etc exist, what are they for?
	KeyMap[SDL_SCANCODE_LSHIFT] = IK_Shift;
	KeyMap[SDL_SCANCODE_RSHIFT] = IK_Shift;
	KeyMap[SDL_SCANCODE_LCTRL] = IK_Ctrl;
	KeyMap[SDL_SCANCODE_RCTRL] = IK_Ctrl;
	KeyMap[SDL_SCANCODE_LALT] = IK_Alt;
	KeyMap[SDL_SCANCODE_RALT] = IK_Alt;
	KeyMap[SDL_SCANCODE_GRAVE] = IK_Tilde;
	KeyMap[SDL_SCANCODE_ESCAPE] = IK_Escape;
	KeyMap[SDL_SCANCODE_SPACE] = IK_Space;
	KeyMap[SDL_SCANCODE_RETURN] = IK_Enter;
	KeyMap[SDL_SCANCODE_BACKSPACE] = IK_Backspace;
	KeyMap[SDL_SCANCODE_CAPSLOCK] = IK_CapsLock;
	KeyMap[SDL_SCANCODE_TAB] = IK_Tab;
	KeyMap[SDL_SCANCODE_DELETE] = IK_Delete;
	KeyMap[SDL_SCANCODE_INSERT] = IK_Insert;
	KeyMap[SDL_SCANCODE_HOME] = IK_Home;
	KeyMap[SDL_SCANCODE_END] = IK_End;
	KeyMap[SDL_SCANCODE_PAGEUP] = IK_PageUp;
	KeyMap[SDL_SCANCODE_PAGEDOWN] = IK_PageDown;
	KeyMap[SDL_SCANCODE_PRINTSCREEN] = IK_PrintScrn;
	KeyMap[SDL_SCANCODE_EQUALS] = IK_Equals;
	KeyMap[SDL_SCANCODE_SEMICOLON] = IK_Semicolon;
	KeyMap[SDL_SCANCODE_BACKSLASH] = IK_Backslash;
	KeyMap[SDL_SCANCODE_SLASH] = IK_Slash;
	KeyMap[SDL_SCANCODE_LEFTBRACKET] = IK_LeftBracket;
	KeyMap[SDL_SCANCODE_RIGHTBRACKET] = IK_RightBracket;
	KeyMap[SDL_SCANCODE_COMMA] = IK_Comma;
	KeyMap[SDL_SCANCODE_PERIOD] = IK_Period;
	KeyMap[SDL_SCANCODE_LEFT] = IK_Left;
	KeyMap[SDL_SCANCODE_UP] = IK_Up;
	KeyMap[SDL_SCANCODE_RIGHT] = IK_Right;
	KeyMap[SDL_SCANCODE_DOWN] = IK_Down;
	KeyMap[SDL_SCANCODE_0] = IK_0;
	KeyMap[SDL_SCANCODE_KP_0] = IK_NumPad0;
	KeyMap[SDL_SCANCODE_KP_PERIOD] = IK_NumPadPeriod;

	INIT_KEY_RANGE( SDL_SCANCODE_1,    SDL_SCANCODE_9,    IK_1,       IK_9 );
	INIT_KEY_RANGE( SDL_SCANCODE_A,    SDL_SCANCODE_Z,    IK_A,       IK_Z );
	INIT_KEY_RANGE( SDL_SCANCODE_KP_1, SDL_SCANCODE_KP_9, IK_NumPad1, IK_NumPad9 );
	INIT_KEY_RANGE( SDL_SCANCODE_F1,   SDL_SCANCODE_F12,  IK_F1,      IK_F12 );
	INIT_KEY_RANGE( SDL_SCANCODE_F13,  SDL_SCANCODE_F24,  IK_F13,     IK_F24 );

	#undef INIT_KEY_RANGE
}

//
// Static init.
//
void UNSDLViewport::InternalClassInitializer( UClass* Class )
{
	guard(UNSDLViewport::InternalClassInitializer);
	// Fill in keymap.
	InitKeyMap();
	unguard;
}

//
// Constructor.
//
UNSDLViewport::UNSDLViewport( ULevel* InLevel, UNSDLClient* InClient )
:	UViewport( InLevel, InClient )
,	Client( InClient )
{
	guard(UNSDLViewport::UNSDLViewport);

	// Set color bytes based on screen resolution.
	SDL_DisplayMode Mode;
	SDL_GetDesktopDisplayMode( InClient->DefaultDisplay, &Mode );
	ColorBytes = SDL_BYTESPERPIXEL( Mode.format );
	Caps = 0;
	if( ColorBytes == 2 && SDL_PIXELLAYOUT( Mode.format ) == SDL_PACKEDLAYOUT_565 )
	{
		Caps |= CC_RGB565;
	}

	// Inherit default display until we have a window.
	DisplayIndex = InClient->DefaultDisplay;
	DisplaySize.w = InClient->GetDefaultDisplayMode().w;
	DisplaySize.h = InClient->GetDefaultDisplayMode().h;

	// Init input.
	if( GIsEditor )
		Input->Init( this, GSystem );

	Destroyed = false;
	QuitRequested = false;
	NativeMiniGL = false;
	NativeMiniGLFullscreen = false;
	MouseCaptured = false;

	unguard;
}

// UObject interface.
void UNSDLViewport::Destroy()
{
	guard(UNSDLViewport::Destroy);
#ifdef AMIGA_USE_NATIVE_MINIGL
	// UViewport::Destroy closes the platform window before it shuts down the
	// renderer. Native MiniGL cannot survive that order: Flush still calls GL
	// while the context and dispatch library would already be gone.
	if( NativeMiniGL && RenDev )
	{
		debugf( NAME_Exit, "Shutting down native MiniGL renderer before context" );
		RenDev->Exit();
		delete RenDev;
		RenDev = NULL;
	}
#endif
	if( Client->FullscreenViewport == this )
	{
		Client->FullscreenViewport = NULL;
	}
	UViewport::Destroy();
	unguard;
}

//
// Set the mouse cursor according to Unreal or UnrealEd's mode, or to
// an hourglass if a slow task is active. Not implemented.
//
void UNSDLViewport::SetModeCursor()
{
	guard(UNSDLViewport::SetModeCursor);
	unguard;
}

//
// Update user viewport interface.
//
void UNSDLViewport::UpdateWindow()
{
	guard(UNSDLViewport::UpdateViewportWindow);

	// If not a window, exit.
	if( hWnd==NULL || OnHold )
		return;

	// Set viewport window's name to show resolution.
	char WindowName[80];
	if( !GIsEditor || (Actor->ShowFlags&SHOW_PlayerCtrl) )
	{
		appSprintf( WindowName, LocalizeGeneral("Product","Core") );
	}
	else switch( Actor->RendMap )
	{
		case REN_Wire:		strcpy(WindowName,LocalizeGeneral("ViewPersp")); break;
		case REN_OrthXY:	strcpy(WindowName,LocalizeGeneral("ViewXY")); break;
		case REN_OrthXZ:	strcpy(WindowName,LocalizeGeneral("ViewXZ")); break;
		case REN_OrthYZ:	strcpy(WindowName,LocalizeGeneral("ViewYZ")); break;
		default:			strcpy(WindowName,LocalizeGeneral("ViewOther")); break;
	}

	// Set window title.
#ifndef PLATFORM_AMIGA
	if( SizeX && SizeY )
	{
		appSprintf(WindowName+strlen(WindowName)," (%i x %i)",SizeX,SizeY);
		if( this == Client->CurrentViewport() )
			strcat( WindowName, " *" );
	}
#endif
#ifdef AMIGA_USE_NATIVE_MINIGL
	if( NativeMiniGL )
		AmigaMiniGLSetTitle( WindowName );
	else
#endif
		SDL_SetWindowTitle( hWnd, WindowName );

	unguard;
}

//
// Open a viewport window.
//
void UNSDLViewport::OpenWindow( void* InParentWindow, UBOOL Temporary, INT NewX, INT NewY, INT OpenX, INT OpenY )
{
	guard(UNSDLViewport::OpenWindow);
	AMIGA_VIEW_LOG( "OpenWindow enter temp=%d size=%dx%d pos=%d,%d", Temporary, NewX, NewY, OpenX, OpenY );
	check(Actor);
	check(!OnHold);
	UBOOL DoRepaint=0, DoSetActive=0;
	UBOOL DoOpenGL=0;
	UBOOL NoHard=ParseParam( appCmdLine(), "nohard" );
	SDL_GLprofile GLProfile = SDL_GL_CONTEXT_PROFILE_COMPATIBILITY;
	NewX = Align(NewX,4);

	if( !Temporary && !GIsEditor && !NoHard )
	{
		// HACK: Just check if we're about to load OpenGLDrv. Not sure how else you would know to add the GL flag.
		char Temp[256] = "";
		GetConfigString( "Engine.Engine", "GameRenderDevice", Temp, ARRAY_COUNT(Temp) );
		appStrupr( Temp );
#ifdef PLATFORM_AMIGA
		// The Amiga build has one runtime-selectable renderer setting for both
		// windowed and fullscreen modes.
		DoOpenGL = appStrstr( Temp, "OPENGL" ) != NULL
			|| appStrstr( Temp, "MINIGL" ) != NULL;
#else
		if( !appStrstr( Temp, "OPENGL" ) )
		{
			GetConfigString( "Engine.Engine", "WindowedRenderDevice", Temp, ARRAY_COUNT(Temp) );
			appStrupr( Temp );
			if( appStrstr( Temp, "OPENGL" ) )
				DoOpenGL = 1;
		}
		else
		{
			DoOpenGL = 1;
		}
#endif
		if( DoOpenGL && appStrstr( Temp, "GLES" ) )
			GLProfile = SDL_GL_CONTEXT_PROFILE_ES;
	}

	// User window of launcher if no parent window was specified.
	if( !InParentWindow )
	{
		QWORD ParentPtr;
		Parse( appCmdLine(), "HWND=", ParentPtr );
		InParentWindow = (void*)ParentPtr;
	}

	if( Temporary )
	{
		// Create in-memory data.
		ColorBytes = 2;
		ScreenPointer = (BYTE*)appMalloc( 2 * NewX * NewY, "TemporaryViewportData" );	
		hWnd = NULL;
		debugf( NAME_Log, "Opened temporary viewport" );
	}
	else
	{
#ifdef AMIGA_USE_NATIVE_MINIGL
		// Renderer changes must also preserve the GL-before-window destruction
		// order. This supports the existing MiniGL/SoftDrv menu without ever
		// handing an Intuition Window pointer to SDL's video functions.
		if( hWnd && NativeMiniGL != !!DoOpenGL )
		{
			if( RenDev )
			{
				RenDev->Exit();
				delete RenDev;
				RenDev = NULL;
			}
			CloseWindow();
		}
		if( DoOpenGL )
		{
			const UBOOL StartFullscreen = Client->StartupFullscreen;
			if( !hWnd )
			{
				AMIGA_VIEW_LOG( "before native MiniGL window %dx%d fullscreen=%d",
					NewX, NewY, (INT)StartFullscreen );
				if( !OpenNativeMiniGLWindow( NewX, NewY, StartFullscreen ) )
					appErrorf( "Could not create native MiniGL window/context: %s", AmigaMiniGLGetOpenError() );
				NativeMiniGL = true;
				NativeMiniGLFullscreen = StartFullscreen;
				hWnd = (SDL_Window*)AmigaMiniGLGetWindow();
				GLCtx = (SDL_GLContext)1;
				SavedX = NewX;
				SavedY = NewY;
				DoSetActive = DoRepaint = 1;
				if( StartFullscreen )
					Client->FullscreenViewport = this;
				debugf( NAME_Log, "Opened native MiniGL viewport" );
			}
			else if( SizeX != NewX || SizeY != NewY )
			{
				RecreateNativeMiniGL( NewX, NewY, NativeMiniGLFullscreen );
			}
			DisplayIndex = Client->DefaultDisplay;
			DisplaySize.w = Client->GetDefaultDisplayMode().w;
			DisplaySize.h = Client->GetDefaultDisplayMode().h;
		}
		else
#endif
		{
		// Get flags.
		DWORD Flags = 0;
		if( InParentWindow && (Actor->ShowFlags & SHOW_ChildWindow) )
		{
			Flags = SDL_WINDOW_SHOWN | SDL_WINDOW_BORDERLESS;
		}
		else
		{
			Flags = SDL_WINDOW_HIDDEN;
		}
		if( DoOpenGL )
		{
			Flags |= SDL_WINDOW_OPENGL;
		}

		// Set OpenGL attributes if needed.
		if( DoOpenGL )
		{
#ifdef PLATFORM_SDL12_COMPAT
			// Amiga SDL 1.2 creates the implicit GL context in
			// SDL_SetVideoMode.  Give AmigaMesa a complete, conservative
			// framebuffer request before that call.
			SDL_GL_SetAttribute( SDL_GL_RED_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_GREEN_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_BLUE_SIZE, 8 );
			SDL_GL_SetAttribute( SDL_GL_DOUBLEBUFFER, 1 );
#else
			if( GLProfile == SDL_GL_CONTEXT_PROFILE_ES )
			{
				// Request GLES2.
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MAJOR_VERSION, 2 );
				SDL_GL_SetAttribute( SDL_GL_CONTEXT_MINOR_VERSION, 0 );
			}
			SDL_GL_SetAttribute( SDL_GL_CONTEXT_PROFILE_MASK, GLProfile );
#endif
		}

		// Set position and size.
		if( OpenX==-1 )
			OpenX = SDL_WINDOWPOS_UNDEFINED;
		if( OpenY==-1 )
			OpenY = SDL_WINDOWPOS_UNDEFINED;

		// If switching renderers, destroy the old window.
		if( hWnd && ( DoOpenGL != !!( SDL_GetWindowFlags( hWnd ) & SDL_WINDOW_OPENGL ) ) )
		{
			CloseWindow();
		}

		// Create or update the window.
			if( !hWnd )
			{
				// Creating new viewport.
				AMIGA_VIEW_LOG( "before SDL_CreateWindow flags=0x%08x gl=%d", (unsigned)Flags, DoOpenGL );
				hWnd = SDL_CreateWindow( "", OpenX, OpenY, NewX, NewY, Flags );
				AMIGA_VIEW_LOG( "after SDL_CreateWindow hwnd=%p error='%s'", (void*)hWnd, SDL_GetError() );
			if( !hWnd && DoOpenGL )
			{
				// Try without GL.
				debugf( NAME_Warning, "Could not create OpenGL window: %s. Trying without OpenGL.", SDL_GetError() );
				Flags &= ~SDL_WINDOW_OPENGL;
				DoOpenGL = 0;
				hWnd = SDL_CreateWindow( "", OpenX, OpenY, NewX, NewY, Flags );
			}
			if( !hWnd )
			{
				appErrorf( "Could not create SDL window: %s", SDL_GetError() );
			}

			// Set parent window.
			if( InParentWindow && (Actor->ShowFlags & SHOW_ChildWindow) )
			{
				SDL_SetWindowModalFor( hWnd, (SDL_Window*)InParentWindow );
			}

			debugf( NAME_Log, "Opened viewport" );
			DoSetActive = DoRepaint = 1;
		}
		else
		{
			// Resizing existing viewport.
			SetClientSize( NewX, NewY, false );
		}

		// Create GL context or SDL renderer if needed.
			if( DoOpenGL )
			{
				if( !GLCtx )
				{
					AMIGA_VIEW_LOG( "before SDL_GL_CreateContext" );
					GLCtx = SDL_GL_CreateContext( hWnd );
					AMIGA_VIEW_LOG( "after SDL_GL_CreateContext ctx=%p", GLCtx );
				if( !GLCtx )
				{
					appErrorf( "Could not create GL context: %s", SDL_GetError() );
				}
			}
				AMIGA_VIEW_LOG( "before SDL_GL_MakeCurrent" );
				SDL_GL_MakeCurrent( hWnd, GLCtx );
				AMIGA_VIEW_LOG( "after SDL_GL_MakeCurrent" );
		}
		else
		{
			SDL_SetHint( SDL_HINT_RENDER_SCALE_QUALITY, "nearest" );
			SDLRen = SDL_CreateRenderer( hWnd, -1, 0 );
			if( !SDLRen )
			{
				// Fallback to software.
				debugf( NAME_Warning, "Could not create SDL renderer: %s. Trying software.", SDL_GetError() );
				SDLRen = SDL_CreateRenderer( hWnd, -1, SDL_RENDERER_SOFTWARE );
				if( !SDLRen )
				{
					appErrorf( "Could not create SDL renderer: %s", SDL_GetError() );
				}
			}
			// Match SoftDrv's staging texture to the selected SDL 1.2 video
			// surface. In RGB565 this halves both framebuffer size and blit
			// traffic, and gives SoftDrv the CC_RGB565 capability it expects.
			{
				SDL_DisplayMode DeskMode;
				SDLTexFormat = ( SDL_GetDesktopDisplayMode( DisplayIndex, &DeskMode ) == 0 && DeskMode.format )
					? DeskMode.format
					: SDL_PIXELFORMAT_RGB565;
			}
			ColorBytes = SDL_BYTESPERPIXEL( SDLTexFormat );
			Caps = ( SDL_PIXELLAYOUT( SDLTexFormat ) == SDL_PACKEDLAYOUT_565 ) ? CC_RGB565 : 0;
			SDLTex = SDL_CreateTexture( SDLRen, SDLTexFormat, SDL_TEXTUREACCESS_STREAMING, NewX, NewY );
			if( !SDLTex )
			{
				appErrorf( "Could not create framebuffer texture: %s", SDL_GetError() );
			}
		}

			AMIGA_VIEW_LOG( "before SDL_ShowWindow" );
			SDL_ShowWindow( hWnd );
			AMIGA_VIEW_LOG( "after SDL_ShowWindow" );

		// Get this window's display parameters.
		SDL_DisplayMode DisplayMode;
		DisplayIndex = SDL_GetWindowDisplayIndex( hWnd );
		if( SDL_GetWindowDisplayMode( hWnd, &DisplayMode ) == 0 )
		{
			DisplaySize.w = DisplayMode.w;
			DisplaySize.h = DisplayMode.h;
		}
		}
	}

	SizeX = NewX;
	SizeY = NewY;

	AMIGA_VIEW_LOG( "before TryRenderDevice current=%p", (void*)RenDev );
	if( !RenDev && Temporary )
		Client->TryRenderDevice( this, "SoftDrv.SoftwareRenderDevice", 0 );
	if( !RenDev && !GIsEditor && !NoHard )
		Client->TryRenderDevice( this, "ini:Engine.Engine.GameRenderDevice", Client->StartupFullscreen );
	if( !RenDev )
		Client->TryRenderDevice( this, "ini:Engine.Engine.WindowedRenderDevice", 0 );
	AMIGA_VIEW_LOG( "after TryRenderDevice rendev=%p", (void*)RenDev );
	check(RenDev);

	if( !Temporary )
		UpdateWindow();
	if( DoRepaint )
	{
		AMIGA_VIEW_LOG( "before first Repaint" );
		Repaint();
		AMIGA_VIEW_LOG( "after first Repaint" );
	}

#ifdef PLATFORM_AMIGA
	// Complete startup capture for windowed mode too. The early SDL grab hides
	// the pointer; this call also installs the raw input.device handler used for
	// unlimited relative mouse movement.
	if( !Temporary && !GIsEditor && Client->CaptureMouse )
		SetMouseCapture( 1, 1, 0 );
#if defined(NSDLDRV_USE_MINIGL) && !defined(AMIGA_USE_NATIVE_MINIGL)
	// Caption setup, viewport adoption and input grabbing happen after the
	// pre-stack centering. Centre once more at the end of that sequence so no
	// intermediate SDL/Intuition operation leaves the window displaced.
	if( !Temporary && hWnd && !(SDL_GetWindowFlags(hWnd) & SDL_WINDOW_FULLSCREEN) )
		AmigaCenterSDLWindow();
#endif
#endif

	unguard;
}

//
// Close a viewport window.  Assumes that the viewport has been opened with
// OpenViewportWindow.  Does not affect the viewport's object, only the
// platform-specific information associated with it.
//
void UNSDLViewport::CloseWindow()
{
	guard(UNSDLViewport::CloseWindow);

	if( hWnd )
	{
#ifdef AMIGA_USE_NATIVE_MINIGL
		if( NativeMiniGL )
		{
			AmigaRawMouseSetCapture( 0 );
			AmigaMiniGLCloseWindow();
			hWnd = NULL;
			GLCtx = NULL;
			NativeMiniGL = false;
			NativeMiniGLFullscreen = false;
			MouseCaptured = false;
			return;
		}
#endif
		if( SDLTex )
		{
			SDL_DestroyTexture( SDLTex );
			SDLTex = NULL;
		}
		if( SDLRen )
		{
			SDL_DestroyRenderer( SDLRen );
			SDLRen = NULL;
		}
		if( GLCtx )
		{
			SDL_GL_DeleteContext( GLCtx );
			GLCtx = NULL;
		}
		SDL_DestroyWindow( hWnd );
		hWnd = NULL;
	}

	unguard;
}

//
// Lock the viewport window and set the approprite Screen and RealScreen fields
// of Viewport.  Returns 1 if locked successfully, 0 if failed.  Note that a
// lock failing is not a critical error; it's a sign that a DirectDraw mode
// has ended or the user has closed a viewport window.
//
UBOOL UNSDLViewport::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* HitData, INT* HitSize )
{
	guard(UNSDLViewport::LockWindow);
	uclock(Client->DrawCycles);

	// Make sure window is lockable.
	if( !hWnd )
	{
		return 0;
	}

	if( OnHold || !SizeX || !SizeY )
	{
		appErrorf( "Failed locking viewport" );
		return 0;
	}

	if( SDLRen && SDLTex )
	{
		// Obtain pointer to screen.
		Stride = SizeX;
		ScreenPointer = NULL;
		SDL_LockTexture( SDLTex, NULL, (void **)&ScreenPointer, &Stride );
		Stride /= ColorBytes;
		check(ScreenPointer);
	}

	// Success.
	uunclock(Client->DrawCycles);

#ifdef AMIGA_USE_NATIVE_MINIGL
	if( NativeMiniGL && !AmigaMiniGLBeginFrame() )
		return 0;
#endif
	const UBOOL Locked = UViewport::Lock( FlashScale, FlashFog, ScreenClear, RenderLockFlags, HitData, HitSize );
#ifdef AMIGA_USE_NATIVE_MINIGL
	if( NativeMiniGL && !Locked ) AmigaMiniGLEndFrame();
#endif
	return Locked;

	unguard;
}

//
// Unlock the viewport window.  If Blit=1, blits the viewport's frame buffer.
//
void UNSDLViewport::Unlock( UBOOL Blit )
{
	guard(UNSDLViewport::Unlock);
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: Viewport Unlock enter blit=%d native=%d", (INT)Blit, (INT)NativeMiniGL );
		AmigaDebugLogf( "[Amiga] AUTO FIRST Viewport Unlock enter blit=%d native=%d", (INT)Blit, (INT)NativeMiniGL );
	}
#endif

	Client->DrawCycles=0;
	uclock(Client->DrawCycles);

	// Unlock base.
	UViewport::Unlock( Blit );
#ifdef AMIGA_USE_NATIVE_MINIGL
	// Release even when Blit is false; the renderer has already flushed.
	if( NativeMiniGL ) AmigaMiniGLEndFrame();
#endif

	// Blit, if desired.
	if( Blit && hWnd && !OnHold )
	{
		if( GLCtx )
		{
			// Flip OpenGL buffers.
#ifdef AMIGA_USE_NATIVE_MINIGL
			if( NativeMiniGL )
			{
#ifdef PLATFORM_AMIGA
				if( GAmigaMiniGLTraceFrames > 0 )
					AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=before-mglSwitchDisplay", (unsigned long)GAmigaFrameSerial );
#endif
#ifdef PLATFORM_AMIGA
				if( GAmigaStartupTraceFrames > 0 )
				{
					debugf( NAME_Init, "FIRST: Viewport Unlock before native swap" );
					AmigaDebugLogf( "[Amiga] AUTO FIRST Viewport Unlock before native swap" );
				}
#endif
				if( GAmigaModeSwitchTraceFrames > 0 )
					AmigaMiniGLModeTrace( "frame: before swap (%d left)", GAmigaModeSwitchTraceFrames );
				AmigaMiniGLSwapBuffers();
				if( GAmigaModeSwitchTraceFrames > 0 )
					AmigaMiniGLModeTrace( "frame: swapped (%d left)", GAmigaModeSwitchTraceFrames-- );
#ifdef PLATFORM_AMIGA
				if( GAmigaMiniGLTraceFrames > 0 )
				{
					AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=after-mglSwitchDisplay", (unsigned long)GAmigaFrameSerial );
					--GAmigaMiniGLTraceFrames;
				}
#endif
			}
			else
#endif
				SDL_GL_SwapWindow( hWnd );
		}
		else if( SDLRen && SDLTex )
		{
			// Blitting with SDLRenderer.
			SDL_UnlockTexture( SDLTex );
			SDL_RenderCopy( SDLRen, SDLTex, NULL, NULL );
			SDL_RenderPresent( SDLRen );
		}
	}

#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: Viewport Unlock after swap" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST Viewport Unlock after swap" );
	}
#endif

	uunclock(Client->DrawCycles);

	unguard;
}

//
// Make this viewport the current one.
// If Viewport=0, makes no viewport the current one.
//
void UNSDLViewport::MakeCurrent()
{
	guard(UNSDLViewport::MakeCurrent);
	Current = 1;
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		UViewport* OldViewport = Client->Viewports(i);
		if( OldViewport->Current && OldViewport != this )
		{
			OldViewport->Current = 0;
			OldViewport->UpdateWindow();
		}
	}
	if( GLCtx
#ifdef AMIGA_USE_NATIVE_MINIGL
		&& !NativeMiniGL
#endif
	)
	{
		SDL_GL_MakeCurrent( hWnd, GLCtx );
	}
	UpdateWindow();
	unguard;
}

//
// Repaint the viewport.
//
void UNSDLViewport::Repaint()
{
	guard(UNSDLViewport::Repaint);
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: Repaint before Engine->Draw" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST Repaint before Engine->Draw" );
	}
#endif
	if( !OnHold && RenDev && SizeX && SizeY )
		Client->Engine->Draw( this, 0 );
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: Repaint after Engine->Draw" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST Repaint after Engine->Draw" );
	}
#endif
	unguard;
}

//
// Set the client size (viewport view size) of a viewport.
//
void UNSDLViewport::SetClientSize( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UNSDLViewport::SetClientSize);

	if( hWnd )
	{
#ifdef AMIGA_USE_NATIVE_MINIGL
		if( NativeMiniGL )
		{
			if( SizeX != NewX || SizeY != NewY )
				RecreateNativeMiniGL( NewX, NewY, NativeMiniGLFullscreen );
		}
		else
#endif
		{
			SDL_SetWindowSize( hWnd, NewX, NewY );
		// Resize output texture if required.
		if( SDLRen && SDLTex )
		{
			SDL_DestroyTexture( SDLTex );
			SDLTex = SDL_CreateTexture( SDLRen, SDLTexFormat, SDL_TEXTUREACCESS_STREAMING, NewX, NewY );
			if( !SDLTex )
			{
				appErrorf( "Could not create framebuffer texture: %s", SDL_GetError() );
			}
		}
		}
	}

	SizeX = NewX;
	SizeY = NewY;

	// Optionally save this size in the profile.
	if( UpdateProfile )
	{
		Client->ViewportX = NewX;
		Client->ViewportY = NewY;
		Client->SaveConfig();
	}

	unguard;
}

#ifdef AMIGA_USE_NATIVE_MINIGL
UBOOL UNSDLViewport::RecreateNativeMiniGL( INT NewX, INT NewY, UBOOL Fullscreen )
{
	guard(UNSDLViewport::RecreateNativeMiniGL);


	const UBOOL HadRenderDevice = RenDev != NULL;
	const UBOOL WasCaptured = MouseCaptured;
	// Mode switches run between frames, while PiStorm3D fullscreen still holds
	// the screen bitmap lock; release it before any other library call.
	AmigaMiniGLReleaseDisplayLock();
	AmigaMiniGLModeTrace( "switch: start %dx%d fs=%d -> %dx%d fs=%d (lock released)",
		SizeX, SizeY, (INT)NativeMiniGLFullscreen, NewX, NewY, (INT)Fullscreen );
	if( HadRenderDevice )
		RenDev->Exit();
	AmigaMiniGLModeTrace( "switch: renderer exited" );

	AmigaMiniGLCloseWindow();
	AmigaMiniGLModeTrace( "switch: old context closed" );
	hWnd = NULL;
	GLCtx = NULL;
	if( !OpenNativeMiniGLWindow( NewX, NewY, Fullscreen ) )
		appErrorf( "Could not recreate native MiniGL window/context at %dx%d", NewX, NewY );
	AmigaMiniGLModeTrace( "switch: new context opened" );

	hWnd = (SDL_Window*)AmigaMiniGLGetWindow();
	GLCtx = (SDL_GLContext)1;
	NativeMiniGL = true;
	NativeMiniGLFullscreen = Fullscreen;
	SizeX = NewX;
	SizeY = NewY;
	if( HadRenderDevice && !RenDev->Init( this ) )
		appErrorf( "Could not reinitialize MiniGL renderer after mode change" );
	if( WasCaptured )
		AmigaMiniGLSetPointerVisible( 0 );
	UpdateWindow();
	AmigaMiniGLModeTrace( "switch: renderer ready" );
	GAmigaModeSwitchTraceFrames = 3;
	return 1;

	unguard;
}
#endif

//
// Return the viewport's window.
//
void* UNSDLViewport::GetWindow()
{
	return (void*)hWnd;
}

//
// Try to make this viewport fullscreen, matching the fullscreen
// mode of the nearest x-size to the current window. If already in
// fullscreen, returns to non-fullscreen.
//
void UNSDLViewport::MakeFullscreen( INT NewX, INT NewY, UBOOL UpdateProfile )
{
	guard(UNSDLViewport::MakeFullscreen);

#ifdef AMIGA_USE_NATIVE_MINIGL
	if( NativeMiniGL )
	{
		if( Client->FullscreenViewport && Client->FullscreenViewport != this )
			Client->EndFullscreen();
		if( Client->FullscreenViewport != this )
		{
			SavedX = SizeX;
			SavedY = SizeY;
		}
		if( !RecreateNativeMiniGL( NewX, NewY, true ) )
			return;
		Client->FullscreenViewport = this;
		if( UpdateProfile )
		{
			Client->ViewportX = NewX;
			Client->ViewportY = NewY;
			Client->StartupFullscreen = 1;
			Client->SaveConfig();
		}
		// Avoid reporting the menu click which selected fullscreen again through
		// input.device. Gameplay capture resumes automatically in TickInput.
		const UBOOL bIsInUI = Console &&
			((UObject*)Console)->GetMainFrame() &&
			((UObject*)Console)->GetMainFrame()->StateNode &&
			((UObject*)Console)->GetMainFrame()->StateNode->GetFName() == "Menuing";
		if( !bIsInUI )
			SetMouseCapture( 1, 1, 0 );
		return;
	}
#endif

	// If another viewport is fullscreen, stop it.  When this viewport merely
	// changes fullscreen resolution, keep it fullscreen: leaving first would
	// restore SavedX/SavedY and make the next toggle jump back to the size used
	// at game startup.
	if( Client->FullscreenViewport && Client->FullscreenViewport != this )
		Client->EndFullscreen();

	const UBOOL WasFullscreen = Client->FullscreenViewport == this;
	if( !WasFullscreen )
	{
		// Save the current (not startup) window size for leaving fullscreen.
		SavedX = SizeX;
		SavedY = SizeY;
	}

	// Fullscreen rendering. For now no borderless.
	SetClientSize( NewX, NewY, false );
	const INT FullscreenResult = SDL_SetWindowFullscreen( hWnd, SDL_WINDOW_FULLSCREEN );
	AMIGA_VIEW_LOG( "SDL_SetWindowFullscreen result=%d error='%s'",
		FullscreenResult, SDL_GetError() );
	if( FullscreenResult < 0 )
	{
		if( WasFullscreen )
			Client->FullscreenViewport = NULL;
		return;
	}
	Client->FullscreenViewport = this;

	if( UpdateProfile )
	{
		Client->ViewportX = NewX;
		Client->ViewportY = NewY;
		Client->StartupFullscreen = 1;
		Client->SaveConfig();
	}

#ifdef NSDLDRV_USE_MINIGL
	// Do not start raw mouse capture while a menu click is still being
	// processed.  On Amiga that would report the same physical button press a
	// second time through input.device and immediately undo this fullscreen
	// toggle.  TickInput restores capture automatically after leaving the UI.
	const UBOOL bIsInUI = Console &&
		((UObject*)Console)->GetMainFrame() &&
		((UObject*)Console)->GetMainFrame()->StateNode &&
		((UObject*)Console)->GetMainFrame()->StateNode->GetFName() == "Menuing";
	if( !bIsInUI )
		SetMouseCapture(1, 1, 0);
#else
	SetMouseCapture(1, 1, 0);
#endif

	unguard;
}

//
//
//
void UNSDLViewport::EndFullscreen()
{
	guard(UNSDLViewport::EndFullscreen);

#ifdef AMIGA_USE_NATIVE_MINIGL
	if( NativeMiniGL )
	{
		RecreateNativeMiniGL( SavedX, SavedY, false );
		return;
	}
#endif

	SDL_SetWindowFullscreen( hWnd, 0 );
	SetClientSize( SavedX, SavedY, false );

	unguard;
}

//
// Update input for viewport.
//
void UNSDLViewport::UpdateInput( UBOOL Reset )
{
	guard(UNSDLViewport::UpdateInput);

	if( Reset )
		appMemset( (void*)JoyAxis, 0, sizeof(JoyAxis) );

	unguard;
}

//
// If the cursor is currently being captured, stop capturing, clipping, and 
// hiding it, and move its position back to where it was when it was initially
// captured.
//
void UNSDLViewport::SetMouseCapture( UBOOL Capture, UBOOL Clip, UBOOL OnlyFocus )
{
	guard(UNSDLViewport::SetMouseCapture);

	// If only focus, reject.
	if( OnlyFocus )
		if(
#ifdef AMIGA_USE_NATIVE_MINIGL
			( NativeMiniGL && !AmigaMiniGLHasFocus() ) ||
			( !NativeMiniGL &&
#endif
			hWnd != SDL_GetMouseFocus()
#ifdef AMIGA_USE_NATIVE_MINIGL
			)
#endif
		)
			return;

	// If capturing, windows requires clipping in order to keep focus.
	Clip |= Capture;

	// A native MiniGL window has no SDL video surface to grab. Keep its capture
	// state locally while input.device supplies the relative mouse deltas.
	MouseCaptured = Capture;
#ifdef AMIGA_USE_NATIVE_MINIGL
	if( !NativeMiniGL )
#endif
		SDL_SetRelativeMouseMode( (SDL_bool)Capture );
#ifdef PLATFORM_AMIGA
	// UE1 releases relative mode while paused or in a menu.  The SDL 1.2
	// compatibility call also shows and ungrabs the Workbench pointer, which
	// leaves it visible over the menu and lets clicks escape the game window.
	// Keep the OS pointer hidden and confined while our window still owns the
	// keyboard focus; raw relative input itself remains disabled below.
	if( !Capture && hWnd
#ifdef AMIGA_USE_NATIVE_MINIGL
		&& !NativeMiniGL
#endif
		&& SDL_GetKeyboardFocus() == hWnd )
	{
		SDL_WM_GrabInput( SDL_GRAB_ON );
		SDL_ShowCursor( SDL_DISABLE );
	}
#ifdef AMIGA_USE_NATIVE_MINIGL
	if( NativeMiniGL )
		AmigaMiniGLSetPointerVisible( Capture ? 0 : 1 );
#endif
	// SDL's amiga_WarpWMCursor() is empty in this SDL 1.2 build. Keep SDL's
	// grab/cursor handling, but obtain unlimited relative deltas directly from
	// one input.device stream handler.
	AmigaRawMouseSetCapture( Capture ? 1 : 0 );
	if( Capture && SizeX > 0 && SizeY > 0
#ifdef AMIGA_USE_NATIVE_MINIGL
		&& !NativeMiniGL
#endif
	)
		SDL_WarpMouse( SizeX / 2, SizeY / 2 );
#endif

	unguard;
}

UBOOL UNSDLViewport::CauseInputEvent( INT iKey, EInputAction Action, FLOAT Delta )
{
	guard(UWindowsViewport::CauseInputEvent);

	// Route to engine if a valid key
	if( iKey > 0 )
		return Client->Engine->InputEvent( this, (EInputKey)iKey, Action, Delta );
	else
		return 0;

	unguard;
}

UBOOL UNSDLViewport::TickInput()
{
	guard(UNSDLViewport::TickInput);

	SDL_Event Ev;
	INT Tmp;
	const FLOAT CurTime = appSeconds();
	const FLOAT DeltaTime = CurTime - InputUpdateTime;
	UBOOL bMouseCaptured = SDL_GetRelativeMouseMode() != SDL_FALSE;

#ifdef PLATFORM_AMIGA
	INT CapturedMouseDX = 0;
	INT CapturedMouseDY = 0;
	// Releasing SDL 1.2's input grab after Escape can enqueue a spurious
	// SDL_QUIT on the Amiga windowed backend. Remember the real Escape briefly
	// so that one synthetic quit does not destroy the only viewport.
	static Uint32 IgnoreQuitAfterEscapeUntil = 0;
	// Capture is enabled explicitly by clicking the game window. Release it in
	// UE1 menus or when the SDL window loses input focus.
	const UBOOL bIsInUI = Console &&
		((UObject*)Console)->GetMainFrame() &&
		((UObject*)Console)->GetMainFrame()->StateNode &&
		((UObject*)Console)->GetMainFrame()->StateNode->GetFName() == "Menuing";
	const UBOOL bHasInputFocus = hWnd &&
#ifdef AMIGA_USE_NATIVE_MINIGL
		( NativeMiniGL ? AmigaMiniGLHasFocus() : SDL_GetKeyboardFocus() == hWnd );
#else
		SDL_GetKeyboardFocus() == hWnd;
#endif
	bMouseCaptured =
#ifdef AMIGA_USE_NATIVE_MINIGL
		NativeMiniGL ? MouseCaptured :
#endif
		SDL_GetRelativeMouseMode() != SDL_FALSE;
	if( bMouseCaptured && (GIsEditor || bIsInUI || !bHasInputFocus) )
	{
		SetMouseCapture( 0, 0, 0 );
		bMouseCaptured = false;
	}
	else if( !bMouseCaptured && !GIsEditor &&
		!bIsInUI && bHasInputFocus && Client->CaptureMouse )
	{
		// Loading a save closes the menu but does not generate a mouse click.
		// Restore gameplay capture as soon as the viewport becomes active again.
		SetMouseCapture( 1, 1, 0 );
		bMouseCaptured = true;
	}
#endif

	while(
#ifdef AMIGA_USE_NATIVE_MINIGL
		( NativeMiniGL && AmigaMiniGLPollEvent( &Ev ) ) ||
#endif
		SDL_PollEvent( &Ev ) )
	{
		switch( Ev.type )
		{
			case SDL_QUIT:
#ifdef PLATFORM_AMIGA
				if(
#ifdef AMIGA_USE_NATIVE_MINIGL
					!NativeMiniGL &&
#endif
					IgnoreQuitAfterEscapeUntil &&
					(INT)(IgnoreQuitAfterEscapeUntil - SDL_GetTicks()) >= 0 )
				{
					IgnoreQuitAfterEscapeUntil = 0;
					break;
				}
#endif
				// signal to client and remember set a flag just in case
				QuitRequested = true;
				return true;
#ifndef PLATFORM_SDL12_COMPAT
			case SDL_TEXTINPUT:
				for( const char *p = Ev.text.text; *p && p < Ev.text.text + sizeof( Ev.text.text ); ++p )
				{
					if( *p < 0 )
						break;
					if( isprint( *p ) || *p == '\r' )
						Client->Engine->Key( this, (EInputKey)*p );
				}
				break;
#endif
			case SDL_KEYDOWN:
#ifdef PLATFORM_AMIGA
				if(
#ifdef AMIGA_USE_NATIVE_MINIGL
					!NativeMiniGL &&
#endif
					Ev.key.keysym.sym == SDLK_ESCAPE )
					IgnoreQuitAfterEscapeUntil = SDL_GetTicks() + 500;
#endif
				if( Ev.key.keysym.sym == SDLK_RETURN && (Ev.key.keysym.mod & KMOD_ALT) )
				{
					Exec("ToggleFullscreen", this);
					break;
				}
#ifdef PLATFORM_SDL12_COMPAT
				// SDL 1.2 has no SDL_TEXTINPUT; deliver printable characters
				// from the key event's unicode field instead.
				if( Ev.type == SDL_KEYDOWN )
				{
					Uint16 U = Ev.key.keysym.unicode;
					// Some Amiga SDL keyboard drivers leave unicode at zero
					// even when translation is enabled. SDLKey values for the
					// printable US-ASCII range and Return are ASCII-compatible.
					if( !U && ((Ev.key.keysym.sym >= 32 && Ev.key.keysym.sym < 127)
						|| Ev.key.keysym.sym == SDLK_RETURN) )
						U = (Uint16)Ev.key.keysym.sym;
					if( U < 128 && ( isprint( U ) || U == '\r' ) )
						Client->Engine->Key( this, (EInputKey)U );
				}
#endif
			case SDL_KEYUP:
#ifdef PLATFORM_SDL12_COMPAT
				// SDL 1.2's scancode is the platform-specific raw key code, while
				// InitKeyMap() above is deliberately indexed by SDLKey/SDLK_*.
				// Using scancode made Escape and most gameplay keys become unrelated
				// Unreal keys on Amiga.
				CauseInputEvent( KeyMap[Ev.key.keysym.sym], ( Ev.type == SDL_KEYDOWN ) ? IST_Press : IST_Release );
#else
				CauseInputEvent( KeyMap[Ev.key.keysym.scancode], ( Ev.type == SDL_KEYDOWN ) ? IST_Press : IST_Release );
#endif
				break;
			case SDL_MOUSEBUTTONDOWN:
			case SDL_MOUSEBUTTONUP:
#ifdef PLATFORM_AMIGA
				// SDL 1.2 has no reliable window-relative mode until input is
				// grabbed. A click inside the gameplay viewport is the explicit
				// request to capture and hide the pointer.
				if( Ev.type == SDL_MOUSEBUTTONDOWN && !GIsEditor && !bIsInUI &&
					!bMouseCaptured )
				{
					SetMouseCapture( 1, 1, 0 );
					bMouseCaptured = true;
				}
#endif
				CauseInputEvent( MouseButtonMap[Ev.button.button], ( Ev.type == SDL_MOUSEBUTTONDOWN ) ? IST_Press : IST_Release );
				break;
#ifdef PLATFORM_SDL12_COMPAT
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP:
				if( Ev.cbutton.button < SDL_CONTROLLER_BUTTON_MAX )
				{
					const UBOOL bIsInUI = Console &&
						((UObject*)Console)->GetMainFrame() &&
						((UObject*)Console)->GetMainFrame()->StateNode &&
						((UObject*)Console)->GetMainFrame()->StateNode->GetFName() == "Menuing";
					const BYTE* JoyMap = bIsInUI ? JoyButtonMapUI : JoyButtonMap;
					CauseInputEvent( JoyMap[Ev.cbutton.button], ( Ev.type == SDL_CONTROLLERBUTTONDOWN ) ? IST_Press : IST_Release );
				}
				break;
			case SDL_CONTROLLERAXISMOTION:
				if( Ev.caxis.axis < SDL_CONTROLLER_AXIS_MAX )
				{
					const BYTE Key = JoyAxisMap[Ev.caxis.axis];
					const INT PrevValue = JoyAxis[Ev.caxis.axis];
					INT NewValue = Clamp( (INT)Ev.caxis.value, -256, 256 );
					INT DeadZone = 0;
					if( Key < IK_JoyX )
					{
						const INT PressThreshold = 64;
						if( PrevValue < PressThreshold && NewValue >= PressThreshold )
							CauseInputEvent( Key, IST_Press );
						else if( PrevValue >= PressThreshold && NewValue < PressThreshold )
							CauseInputEvent( Key, IST_Release );
					}
					else
					{
						if( Key >= IK_JoyX && Key <= IK_JoyZ )
							DeadZone = Client->DeadZoneXYZ * 256.f;
						else if( Key == IK_JoyR || Key == IK_JoyU || Key == IK_JoyV )
							DeadZone = Client->DeadZoneRUV * 256.f;
						if( Abs(NewValue) < DeadZone )
							NewValue = 0;
					}
					JoyAxis[Ev.caxis.axis] = NewValue;
				}
				break;
#endif
#ifndef PLATFORM_SDL12_COMPAT
			case SDL_MOUSEWHEEL:
				if( Ev.wheel.y )
				{
					CauseInputEvent( IK_MouseW, IST_Axis, Ev.wheel.y );
					if( Ev.wheel.y < 0 )
					{
						CauseInputEvent( IK_MouseWheelDown, IST_Press );
						CauseInputEvent( IK_MouseWheelDown, IST_Release );
					}
					else if( Ev.wheel.y > 0 )
					{
						CauseInputEvent( IK_MouseWheelUp, IST_Press );
						CauseInputEvent( IK_MouseWheelUp, IST_Release );
					}
				}
				break;
			case SDL_CONTROLLERBUTTONDOWN:
			case SDL_CONTROLLERBUTTONUP:
				{
					// HACK: Swap to alternate bindings when in menus, but not when waiting for keypress in the keybind menu.
					const UBOOL bIsInUI = Console &&
						((UObject*)Console)->GetMainFrame() &&
						((UObject*)Console)->GetMainFrame()->StateNode &&
						((UObject*)Console)->GetMainFrame()->StateNode->GetFName() == "Menuing";
					const BYTE* JoyMap = bIsInUI ? JoyButtonMapUI : JoyButtonMap;
					CauseInputEvent( JoyMap[Ev.cbutton.button], ( Ev.type == SDL_CONTROLLERBUTTONDOWN ) ? IST_Press : IST_Release );
				}
				break;
			case SDL_CONTROLLERAXISMOTION:
				{
					const BYTE Key = JoyAxisMap[Ev.caxis.axis];
					const INT PrevValue = JoyAxis[Ev.caxis.axis];
					INT NewValue = Ev.caxis.value;
					INT DeadZone = 0;
					if ( Key < IK_JoyX )
					{
						// Treat the axis like a trigger.
						if ( PrevValue < JoyAxisPressThreshold && NewValue >= JoyAxisPressThreshold )
							CauseInputEvent( Key, IST_Press );
						else if ( PrevValue >= JoyAxisPressThreshold && NewValue < JoyAxisPressThreshold )
							CauseInputEvent( Key, IST_Release );
					}
					else
					{
						// Apply deadzone.
						if ( Key >= IK_JoyX && Key <= IK_JoyZ )
							DeadZone = Client->DeadZoneXYZ * 32767.f;
						else if ( Key == IK_JoyR || Key == IK_JoyU || Key == IK_JoyV )
							DeadZone = Client->DeadZoneRUV * 32767.f;
						if ( Abs(NewValue) < DeadZone )
							NewValue = 0;
					}
					JoyAxis[Ev.caxis.axis] = NewValue;
				}
				break;
#endif // !PLATFORM_SDL12_COMPAT (SDL 1.2 has no wheel/gamecontroller events)
			case SDL_MOUSEMOTION:
#ifdef PLATFORM_AMIGA
				if( bMouseCaptured )
				{
					// Keep SDL deltas as a fallback in case input.device could
					// not be installed on this particular system.
					CapturedMouseDX += Ev.motion.xrel;
					CapturedMouseDY += Ev.motion.yrel;
					break;
				}
#endif
				if( !Client->FullscreenViewport && !bMouseCaptured )
				{
					// If cursor isn't captured, just do MousePosition.
					Client->Engine->MousePosition( this, 0, Ev.motion.x, Ev.motion.y );
				}
				else
				{
					INT MouseDX = Ev.motion.xrel;
					INT MouseDY = -Ev.motion.yrel;
					DWORD ViewportButtonFlags = 0;
					if( Ev.motion.state & SDL_BUTTON_LMASK ) ViewportButtonFlags |= MOUSE_Left;
					if( Ev.motion.state & SDL_BUTTON_RMASK ) ViewportButtonFlags |= MOUSE_Right;
					if( Ev.motion.state & SDL_BUTTON_MMASK ) ViewportButtonFlags |= MOUSE_Middle;
					if( MouseDX || MouseDY )
					{
						Client->Engine->MouseDelta( this, ViewportButtonFlags, MouseDX, MouseDY );
						if( MouseDX ) CauseInputEvent( IK_MouseX, IST_Axis, MouseDX );
						if( MouseDY ) CauseInputEvent( IK_MouseY, IST_Axis, MouseDY );
					}
				}
				break;
			default:
				break;
		}
	}

#ifdef PLATFORM_AMIGA
	// input.device supplies physical relative deltas even after Intuition's
	// pointer has reached a window or screen edge; no cursor warp is needed.
	if( bMouseCaptured )
	{
		INT RawMouseDX = 0;
		INT RawMouseDY = 0;
		unsigned int RawPressed = 0;
		unsigned int RawReleased = 0;
		unsigned int RawHeld = 0;
		const UBOOL HaveRawInput = AmigaRawMouseRead(
			&RawMouseDX, &RawMouseDY, &RawPressed, &RawReleased, &RawHeld );
		if( !HaveRawInput )
		{
			RawMouseDX = CapturedMouseDX;
			RawMouseDY = CapturedMouseDY;
		}
		if( RawPressed & (1U << 0) ) CauseInputEvent( IK_LeftMouse,   IST_Press );
		if( RawPressed & (1U << 1) ) CauseInputEvent( IK_MiddleMouse, IST_Press );
		if( RawPressed & (1U << 2) ) CauseInputEvent( IK_RightMouse,  IST_Press );
		if( RawReleased & (1U << 0) ) CauseInputEvent( IK_LeftMouse,   IST_Release );
		if( RawReleased & (1U << 1) ) CauseInputEvent( IK_MiddleMouse, IST_Release );
		if( RawReleased & (1U << 2) ) CauseInputEvent( IK_RightMouse,  IST_Release );
		if( RawMouseDX || RawMouseDY )
		{
			DWORD ViewportButtonFlags = 0;
			if( RawHeld & (1U << 0) ) ViewportButtonFlags |= MOUSE_Left;
			if( RawHeld & (1U << 2) ) ViewportButtonFlags |= MOUSE_Right;
			if( RawHeld & (1U << 1) ) ViewportButtonFlags |= MOUSE_Middle;
			RawMouseDY = -RawMouseDY;
			Client->Engine->MouseDelta( this, ViewportButtonFlags, RawMouseDX, RawMouseDY );
			if( RawMouseDX ) CauseInputEvent( IK_MouseX, IST_Axis, RawMouseDX );
			if( RawMouseDY ) CauseInputEvent( IK_MouseY, IST_Axis, RawMouseDY );
#ifdef AMIGA_USE_NATIVE_MINIGL
			if( !NativeMiniGL )
#endif
				SDL_WarpMouse( SizeX / 2, SizeY / 2 );
		}
	}
#endif

	// Constantly hammer the input system with axis events for axes that are not zero.
	for ( INT i = 0; i < SDL_CONTROLLER_AXIS_MAX; ++i )
	{
		const BYTE Key = JoyAxisMap[i];
		const SWORD Value = JoyAxis[i];
		if ( Value && Key && Key >= IK_JoyX )
		{
#ifdef PLATFORM_SDL12_COMPAT
			const FLOAT FltValue = Clamp( Value / 256.f, -1.f, 1.f );
#else
			const FLOAT FltValue = Clamp( Value / 32767.f, -1.f, 1.f );
#endif
			FLOAT Scale = ( Key >= IK_JoyX && Key <= IK_JoyZ ) ? Client->ScaleXYZ : Client->ScaleRUV;
			Scale *= JoyAxisDefaultScale[i] * DeltaTime;
			if ( ( Client->InvertV && Key == IK_JoyV ) || ( Client->InvertY && Key == IK_JoyY ) )
				Scale = -Scale;
			CauseInputEvent( Key, IST_Axis, FltValue * Scale );
		}
	}

	InputUpdateTime = CurTime;

	return QuitRequested;

	unguard;
}

/*-----------------------------------------------------------------------------
	Command line.
-----------------------------------------------------------------------------*/

UBOOL UNSDLViewport::Exec( const char* Cmd, FOutputDevice* Out )
{
	guard(UNSDLViewport::Exec);
	const char* RenderCmd = Cmd;
	if( ParseCommand( &RenderCmd, "GetRenderDevice" ) )
	{
		char RenderClass[256] = "";
		GetConfigString( "Engine.Engine", "GameRenderDevice",
			RenderClass, ARRAY_COUNT(RenderClass) );
		Out->Log( RenderClass );
		return 1;
	}
	else if( ParseCommand( &RenderCmd, "SetRenderDevice" ) )
	{
		const char* RenderClass =
			ParseCommand( &RenderCmd, "Software" )
			? "SoftDrv.SoftwareRenderDevice"
		#ifdef NSDLDRV_USE_MINIGL
			: "NMiniGLDrv.NMiniGLRenderDevice";
		#else
			: "NOpenGLDrv.NOpenGLRenderDevice";
		#endif
		// Write the exact INI keys directly. The generic UnrealScript SET
		// command only handles reflected class properties and silently ignored
		// the legacy WindowedRenderDevice/RenderDevice keys.
		SetConfigString( "Engine.Engine", "GameRenderDevice", RenderClass );
		SetConfigString( "Engine.Engine", "WindowedRenderDevice", RenderClass );
		SetConfigString( "Engine.Engine", "RenderDevice", RenderClass );
		Out->Log( RenderClass );
		return 1;
	}
	else if( ParseCommand( &RenderCmd, "GetTextureFiltering" ) )
	{
#ifdef NSDLDRV_USE_MINIGL
		// Query the live renderer, not the obsolete NoFiltering INI key.
		if(RenDev && RenDev->Exec("GetTextureFiltering",Out)) return 1;
#endif
		char RenderClass[256] = "";
		GetConfigString( "Engine.Engine", "GameRenderDevice",
			RenderClass, ARRAY_COUNT(RenderClass) );
		INT Enabled = 1;
		if( appStrstr( appStrupr(RenderClass), "SOFTDRV" ) )
		{
			GetConfigInt( "SoftDrv.SoftwareRenderDevice",
				"HighResTextureSmooth", Enabled );
		}
		else
		{
			INT NoFiltering = 0;
		#ifdef NSDLDRV_USE_MINIGL
			GetConfigInt( "NMiniGLDrv.NMiniGLRenderDevice",
		#else
			GetConfigInt( "NOpenGLDrv.NOpenGLRenderDevice",
		#endif
				"NoFiltering", NoFiltering );
			Enabled = !NoFiltering;
		}
		Out->Log( Enabled ? "True" : "False" );
		return 1;
	}
	else if( ParseCommand( &RenderCmd, "SetTextureFiltering" ) )
	{
		const UBOOL Enabled = ParseCommand( &RenderCmd, "On" );
#ifdef NSDLDRV_USE_MINIGL
		// MiniGL updates cached texture filters on use; no full flush needed.
		if(RenDev && RenDev->Exec(Enabled ? "SetTextureFiltering On" : "SetTextureFiltering Off",Out)) return 1;
#endif
		char RenderClass[256] = "";
		GetConfigString( "Engine.Engine", "GameRenderDevice",
			RenderClass, ARRAY_COUNT(RenderClass) );
		if( appStrstr( appStrupr(RenderClass), "SOFTDRV" ) )
		{
			const char* Value = Enabled ? "True" : "False";
			SetConfigString( "SoftDrv.SoftwareRenderDevice",
				"HighResTextureSmooth", Value );
			SetConfigString( "SoftDrv.SoftwareRenderDevice",
				"LowResTextureSmooth", Value );
			char SetCommand[128];
			appSprintf( SetCommand,
				"set SoftDrv.SoftwareRenderDevice HighResTextureSmooth %s", Value );
			GObj.Exec( SetCommand, Out );
			appSprintf( SetCommand,
				"set SoftDrv.SoftwareRenderDevice LowResTextureSmooth %s", Value );
			GObj.Exec( SetCommand, Out );
		}
		else
		{
			const char* Value = Enabled ? "False" : "True";
		#ifdef NSDLDRV_USE_MINIGL
			SetConfigString( "NMiniGLDrv.NMiniGLRenderDevice",
		#else
			SetConfigString( "NOpenGLDrv.NOpenGLRenderDevice",
		#endif
				"NoFiltering", Value );
			char SetCommand[128];
		#ifdef NSDLDRV_USE_MINIGL
			appSprintf( SetCommand,
				"set NMiniGLDrv.NMiniGLRenderDevice NoFiltering %s", Value );
		#else
			appSprintf( SetCommand,
				"set NOpenGLDrv.NOpenGLRenderDevice NoFiltering %s", Value );
		#endif
			GObj.Exec( SetCommand, Out );
		}
		if( RenDev )
			RenDev->Flush();
		Out->Log( Enabled ? "True" : "False" );
		return 1;
	}
	else if( UViewport::Exec( Cmd, Out ) )
	{
		return 1;
	}
	else if( ParseCommand(&Cmd, "ToggleFullscreen") )
	{
		// Toggle fullscreen.
		if( Client->FullscreenViewport )
		{
			Client->EndFullscreen();
			Client->StartupFullscreen = 0;
			Client->SaveConfig();
		}
		else if( !(Actor->ShowFlags & SHOW_ChildWindow) )
		{
			// SDL uses the same render device in a window and fullscreen.
			// Recreating it here can reset the viewport to its startup size;
			// enter fullscreen directly using the resolution currently shown.
			MakeFullscreen( SizeX, SizeY, 1 );
		}
		return 1;
	}
	else if( ParseCommand(&Cmd, "GetCurrentRes") )
	{
		Out->Logf( "%ix%i", SizeX, SizeY );
		return 1;
	}
	else if( ParseCommand(&Cmd, "SetRes") )
	{
		INT X=appAtoi(Cmd), Y=appAtoi(appStrchr(Cmd,'x') ? appStrchr(Cmd,'x')+1 : appStrchr(Cmd,'X') ? appStrchr(Cmd,'X')+1 : "");
		if( X && Y )
		{
			if( Client->FullscreenViewport )
				MakeFullscreen( X, Y, 1 );
			else
				SetClientSize( X, Y, 1 );
		}
		return 1;
	}
	else if( ParseCommand(&Cmd, "Preferences") )
	{
		if( Client->FullscreenViewport )
			Client->EndFullscreen();
		return 1;
	}
	else return 0;
	unguard;
}
