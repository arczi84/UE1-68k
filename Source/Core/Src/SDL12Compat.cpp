/*=============================================================================
	SDL12Compat.cpp: SDL2-on-SDL1.2 compatibility shim implementation.

	See SDL12Compat.h. Only the SDL2 subset the engine actually uses is
	implemented.
=============================================================================*/

#include "SDL12Compat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
#endif

/* Opaque handle for the single implicit SDL 1.2 window. */
static int SDLC_WindowToken;
SDL_Window* SDLC_TheWindow = NULL;
Uint32      SDLC_WindowFlags = 0;

static int SDLC_WindowW = 0;
static int SDLC_WindowH = 0;
static int SDLC_RelativeMouse = 0;
static char* SDLC_ClipboardText = NULL;

/*-----------------------------------------------------------------------------
	Windows.
-----------------------------------------------------------------------------*/

// Translate SDL2 window flags to SDL 1.2 SDL_SetVideoMode flags.
static Uint32 SDLC_VideoFlags( Uint32 Flags )
{
	Uint32 Out = 0;
	if( Flags & SDL_WINDOW_FULLSCREEN )
		Out |= SDL_FULLSCREEN;
	if( Flags & SDL_WINDOW_OPENGL )
		Out |= SDL_OPENGL;
	if( Flags & SDL_WINDOW_BORDERLESS )
		Out |= SDL_NOFRAME;
	return Out;
}

// SDL 1.2 selects the display pixel format while creating the video surface.
// Passing the actual desktop depth is more reliable on CyberGraphX/AmigaMesa
// than asking the backend to infer it from a zero depth.
static int SDLC_VideoBpp()
{
	const SDL_VideoInfo* Info = SDL_GetVideoInfo();
	return ( Info && Info->vfmt && Info->vfmt->BitsPerPixel )
		? Info->vfmt->BitsPerPixel
		: 16;
}

SDL_Window* SDL_CreateWindow( const char* Title, int X, int Y, int W, int H, Uint32 Flags )
{
	(void)X; (void)Y;
	if( SDLC_TheWindow )
	{
#ifdef PLATFORM_AMIGA
		AmigaDebugLogf( "[Amiga] SDL12: reusing early window %dx%d for %dx%d", SDLC_WindowW, SDLC_WindowH, W, H );
#endif
		SDLC_WindowFlags = Flags;
		if( Title )
			SDL_WM_SetCaption( Title, Title );
		return SDLC_TheWindow;
	}

	const int Bpp = SDLC_VideoBpp();
	const Uint32 VideoFlags = SDLC_VideoFlags( Flags );
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] SDL12: SDL_SetVideoMode %dx%dx%d flags=0x%08lx", W, H, Bpp, (unsigned long)VideoFlags );
#endif
	if( !SDL_SetVideoMode( W, H, Bpp, VideoFlags ) )
		return NULL;

	SDLC_WindowFlags = Flags;
	SDLC_WindowW = W;
	SDLC_WindowH = H;
	SDLC_TheWindow = (SDL_Window*)&SDLC_WindowToken;

	if( Title )
		SDL_WM_SetCaption( Title, Title );

	return SDLC_TheWindow;
}

void SDL_DestroyWindow( SDL_Window* Win )
{
	(void)Win;
	// SDL 1.2 tears the video surface down in SDL_Quit.
	SDLC_TheWindow = NULL;
	SDLC_WindowFlags = 0;
}

void SDL_SetWindowTitle( SDL_Window* Win, const char* Title )
{
	(void)Win;
	if( Title )
		SDL_WM_SetCaption( Title, Title );
}

void SDL_SetWindowSize( SDL_Window* Win, int W, int H )
{
	(void)Win;
	if( SDL_SetVideoMode( W, H, SDLC_VideoBpp(), SDLC_VideoFlags( SDLC_WindowFlags ) ) )
	{
		SDLC_WindowW = W;
		SDLC_WindowH = H;
	}
}

int SDL_SetWindowFullscreen( SDL_Window* Win, Uint32 Flags )
{
	(void)Win;
	if( Flags & SDL_WINDOW_FULLSCREEN )
		SDLC_WindowFlags |= SDL_WINDOW_FULLSCREEN;
	else
		SDLC_WindowFlags &= ~SDL_WINDOW_FULLSCREEN;

	return SDL_SetVideoMode( SDLC_WindowW, SDLC_WindowH, SDLC_VideoBpp(), SDLC_VideoFlags( SDLC_WindowFlags ) ) ? 0 : -1;
}

Uint32 SDL_GetWindowFlags( SDL_Window* Win )
{
	(void)Win;
	return SDLC_WindowFlags;
}

void SDL_ShowWindow( SDL_Window* Win )
{
	(void)Win;
	SDLC_WindowFlags |= SDL_WINDOW_SHOWN;
	SDLC_WindowFlags &= ~SDL_WINDOW_HIDDEN;
}

void SDL_GetWindowSize( SDL_Window* Win, int* W, int* H )
{
	(void)Win;
	SDL_Surface* Surf = SDL_GetVideoSurface();
	if( W ) *W = Surf ? Surf->w : SDLC_WindowW;
	if( H ) *H = Surf ? Surf->h : SDLC_WindowH;
}

int SDL_SetWindowBrightness( SDL_Window* Win, float Brightness )
{
	(void)Win;
	return SDL_SetGamma( Brightness, Brightness, Brightness );
}

int SDL_GetWindowDisplayIndex( SDL_Window* Win )
{
	(void)Win;
	return 0;
}

SDL_Window* SDL_GetKeyboardFocus( void )
{
	return ( SDL_GetAppState() & SDL_APPINPUTFOCUS ) ? SDLC_TheWindow : NULL;
}

SDL_Window* SDL_GetMouseFocus( void )
{
	return ( SDL_GetAppState() & SDL_APPMOUSEFOCUS ) ? SDLC_TheWindow : NULL;
}

/*-----------------------------------------------------------------------------
	GL context. SDL 1.2 makes the context current as part of SDL_SetVideoMode,
	so these are mostly bookkeeping.
-----------------------------------------------------------------------------*/

SDL_GLContext SDL_GL_CreateContext( SDL_Window* Win )
{
	(void)Win;
	// Non-NULL so callers treating this as a failure check succeed.
	return (SDL_GLContext)&SDLC_WindowToken;
}

void SDL_GL_DeleteContext( SDL_GLContext Ctx )
{
	(void)Ctx;
}

int SDL_GL_MakeCurrent( SDL_Window* Win, SDL_GLContext Ctx )
{
	(void)Win; (void)Ctx;
	return 0;
}

void SDL_GL_SwapWindow( SDL_Window* Win )
{
	(void)Win;
	SDL_GL_SwapBuffers();
}

int SDL_GL_SetSwapInterval( int Interval )
{
#ifdef SDL_GL_SWAP_CONTROL
	return SDL_GL_SetAttribute( SDL_GL_SWAP_CONTROL, Interval );
#else
	(void)Interval;
	return -1;
#endif
}

/*-----------------------------------------------------------------------------
	Display modes, via SDL_ListModes.
-----------------------------------------------------------------------------*/

static SDL_Rect** SDLC_ListModes( void )
{
	return SDL_ListModes( NULL, SDL_FULLSCREEN );
}

int SDL_GetNumDisplayModes( int DisplayIndex )
{
	(void)DisplayIndex;
	SDL_Rect** Modes = SDLC_ListModes();
	// NULL: no modes. (SDL_Rect**)-1: any mode is allowed.
	if( !Modes || Modes == (SDL_Rect**)-1 )
		return 0;

	int Count = 0;
	while( Modes[Count] )
		++Count;
	return Count;
}

int SDL_GetDisplayMode( int DisplayIndex, int ModeIndex, SDL_DisplayMode* Mode )
{
	(void)DisplayIndex;
	if( !Mode )
		return -1;

	SDL_Rect** Modes = SDLC_ListModes();
	if( !Modes || Modes == (SDL_Rect**)-1 )
		return -1;

	int Count = 0;
	while( Modes[Count] )
		++Count;
	if( ModeIndex < 0 || ModeIndex >= Count )
		return -1;

	memset( Mode, 0, sizeof(*Mode) );
	Mode->w = Modes[ModeIndex]->w;
	Mode->h = Modes[ModeIndex]->h;
	return 0;
}

int SDL_GetDesktopDisplayMode( int DisplayIndex, SDL_DisplayMode* Mode )
{
	(void)DisplayIndex;
	if( !Mode )
		return -1;

	const SDL_VideoInfo* Info = SDL_GetVideoInfo();
	if( !Info )
		return -1;

	memset( Mode, 0, sizeof(*Mode) );
	Mode->w = Info->current_w;
	Mode->h = Info->current_h;
	return 0;
}

int SDL_GetWindowDisplayMode( SDL_Window* Win, SDL_DisplayMode* Mode )
{
	(void)Win;
	if( !Mode )
		return -1;

	memset( Mode, 0, sizeof(*Mode) );
	SDL_Surface* Surf = SDL_GetVideoSurface();
	Mode->w = Surf ? Surf->w : SDLC_WindowW;
	Mode->h = Surf ? Surf->h : SDLC_WindowH;
	return 0;
}

/*-----------------------------------------------------------------------------
	Timing. SDL 1.2 only offers millisecond ticks.
-----------------------------------------------------------------------------*/

Uint64 SDL_GetPerformanceCounter( void )
{
	return (Uint64)SDL_GetTicks();
}

Uint64 SDL_GetPerformanceFrequency( void )
{
	return 1000;
}

/*-----------------------------------------------------------------------------
	System queries.
-----------------------------------------------------------------------------*/

const char* SDL_GetPlatform( void )
{
	return "AmigaOS";
}

int SDL_GetCPUCount( void )
{
	return 1;
}

char* SDL_GetBasePath( void )
{
	// SDL2 callers free this with SDL_free, so allocate the same way.
	// PROGDIR: is the AmigaOS path to the running executable's directory.
	const char* Base = "PROGDIR:";
	char* Result = (char*)SDL_malloc( strlen(Base) + 1 );
	if( Result )
		strcpy( Result, Base );
	return Result;
}

/*-----------------------------------------------------------------------------
	Clipboard. SDL 1.2 has no clipboard API; keep a process-local one so the
	engine's copy/paste at least works within the game.
-----------------------------------------------------------------------------*/

int SDL_SetClipboardText( const char* Text )
{
	if( SDLC_ClipboardText )
	{
		free( SDLC_ClipboardText );
		SDLC_ClipboardText = NULL;
	}
	if( Text )
	{
		SDLC_ClipboardText = (char*)malloc( strlen(Text) + 1 );
		if( !SDLC_ClipboardText )
			return -1;
		strcpy( SDLC_ClipboardText, Text );
	}
	return 0;
}

char* SDL_GetClipboardText( void )
{
	// SDL2 semantics: caller frees with SDL_free, never returns NULL.
	const char* Src = SDLC_ClipboardText ? SDLC_ClipboardText : "";
	char* Result = (char*)SDL_malloc( strlen(Src) + 1 );
	if( Result )
		strcpy( Result, Src );
	return Result;
}

SDL_bool SDL_HasClipboardText( void )
{
	return ( SDLC_ClipboardText && *SDLC_ClipboardText ) ? SDL_TRUE : SDL_FALSE;
}

/*-----------------------------------------------------------------------------
	Relative mouse mode, via SDL 1.2 input grabbing.
-----------------------------------------------------------------------------*/

int SDL_SetRelativeMouseMode( SDL_bool Enabled )
{
	SDLC_RelativeMouse = ( Enabled == SDL_TRUE );
	SDL_WM_GrabInput( SDLC_RelativeMouse ? SDL_GRAB_ON : SDL_GRAB_OFF );
	SDL_ShowCursor( SDLC_RelativeMouse ? SDL_DISABLE : SDL_ENABLE );
	return 0;
}

SDL_bool SDL_GetRelativeMouseMode( void )
{
	return SDLC_RelativeMouse ? SDL_TRUE : SDL_FALSE;
}

void SDL_StartTextInput( void )
{
	SDL_EnableUNICODE( 1 );
	SDL_EnableKeyRepeat( SDL_DEFAULT_REPEAT_DELAY, SDL_DEFAULT_REPEAT_INTERVAL );
}

/*-----------------------------------------------------------------------------
	Message boxes. No GUI dialog in SDL 1.2, so log to stderr.
-----------------------------------------------------------------------------*/

int SDL_ShowSimpleMessageBox( Uint32 Flags, const char* Title, const char* Message, SDL_Window* Win )
{
	(void)Flags; (void)Win;
	fprintf( stderr, "%s: %s\n", Title ? Title : "Message", Message ? Message : "" );
	fflush( stderr );
	return 0;
}

/*-----------------------------------------------------------------------------
	Renderer / Texture. Amiga always uses the GL path, so these are never
	called; provide stubs that fail cleanly so the software path is skipped.
-----------------------------------------------------------------------------*/

SDL_Renderer* SDL_CreateRenderer( SDL_Window* Win, int Index, Uint32 Flags )
{
	(void)Win; (void)Index; (void)Flags;
	return NULL;
}

void SDL_DestroyRenderer( SDL_Renderer* Ren )
{
	(void)Ren;
}

SDL_Texture* SDL_CreateTexture( SDL_Renderer* Ren, Uint32 Format, int Access, int W, int H )
{
	(void)Ren; (void)Format; (void)Access; (void)W; (void)H;
	return NULL;
}

void SDL_DestroyTexture( SDL_Texture* Tex )
{
	(void)Tex;
}

int SDL_LockTexture( SDL_Texture* Tex, const SDL_Rect* Rect, void** Pixels, int* Pitch )
{
	(void)Tex; (void)Rect;
	if( Pixels ) *Pixels = NULL;
	if( Pitch ) *Pitch = 0;
	return -1;
}

void SDL_UnlockTexture( SDL_Texture* Tex )
{
	(void)Tex;
}

int SDL_RenderCopy( SDL_Renderer* Ren, SDL_Texture* Tex, const SDL_Rect* Src, const SDL_Rect* Dst )
{
	(void)Ren; (void)Tex; (void)Src; (void)Dst;
	return -1;
}

void SDL_RenderPresent( SDL_Renderer* Ren )
{
	(void)Ren;
}

int SDL_ShowMessageBox( const SDL_MessageBoxData* Data, int* ButtonId )
{
	if( !Data )
		return -1;

	fprintf( stderr, "%s: %s\n", Data->title ? Data->title : "Message",
		Data->message ? Data->message : "" );
	fflush( stderr );

	// No way to prompt; pick the default button so callers can proceed.
	if( ButtonId )
	{
		*ButtonId = Data->numbuttons > 0 ? Data->buttons[0].buttonid : 0;
		for( int i=0; i<Data->numbuttons; ++i )
		{
			if( Data->buttons[i].flags & SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT )
			{
				*ButtonId = Data->buttons[i].buttonid;
				break;
			}
		}
	}
	return 0;
}
