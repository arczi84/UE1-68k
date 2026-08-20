/*=============================================================================
	SDL12Compat.cpp: SDL2-on-SDL1.2 compatibility shim implementation.

	See SDL12Compat.h. Only the SDL2 subset the engine actually uses is
	implemented.
=============================================================================*/

#include "SDL12Compat.h"

#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
#endif

// Declared here rather than including UnFile.h: this shim is deliberately
// free of engine headers (their BYTE/WORD typedefs clash with exec/types.h).
extern int GetConfigInt( const char* Section, const char* Key, int& Value, const char* FileName );

#ifdef PLATFORM_AMIGA
static void SDLC_EngineLog( const char* Msg )
{
	// The RGB conversion trace was useful while fixing the 16-bit channel
	// order, but it must stay silent in the normal build: no console output
	// and no extra disk writes.
	(void)Msg;
}

static void SDLC_FullscreenLogf( const char* Fmt, ... )
{
	static int FirstWrite = 1;
	FILE* F = fopen( "PROGDIR:fullscreen.log", FirstWrite ? "w" : "a" );
	if( !F )
		return;
	FirstWrite = 0;

	va_list Args;
	va_start( Args, Fmt );
	vfprintf( F, Fmt, Args );
	va_end( Args );
	fputc( '\n', F );
	fclose( F );
}

static void SDLC_LogVideoMode( const char* Operation, int W, int H, int Bpp,
	Uint32 Flags, SDL_Surface* Surface )
{
	char Driver[64] = "";
	SDL_VideoDriverName( Driver, sizeof(Driver) );
	const int ModeOK = SDL_VideoModeOK( W, H, Bpp, Flags );
	SDLC_FullscreenLogf(
		"%s: driver='%s' request=%dx%dx%d flags=%08lx modeOK=%d result=%p error='%s'",
		Operation, Driver, W, H, Bpp, (unsigned long)Flags, ModeOK,
		(void*)Surface, SDL_GetError() );

	if( Surface && Surface->format )
	{
		SDLC_FullscreenLogf(
			"%s: actual=%dx%dx%d bytes=%d pitch=%d flags=%08lx fullscreen=%d "
			"masks R=%08lx G=%08lx B=%08lx A=%08lx",
			Operation, Surface->w, Surface->h,
			(int)Surface->format->BitsPerPixel,
			(int)Surface->format->BytesPerPixel, (int)Surface->pitch,
			(unsigned long)Surface->flags,
			(Surface->flags & SDL_FULLSCREEN) != 0,
			(unsigned long)Surface->format->Rmask,
			(unsigned long)Surface->format->Gmask,
			(unsigned long)Surface->format->Bmask,
			(unsigned long)Surface->format->Amask );
	}
}
#endif

// Runtime-selectable channel order for the 32bpp blit, so the correct one can
// be found without a rebuild: [NSDLDrv.NSDLClient] ChannelOrder = 0..5.
static int SDLC_ChannelOrder()
{
	static int Cached = -1;
	if( Cached < 0 )
	{
		int V = 0;
		if( !GetConfigInt( "NSDLDrv.NSDLClient", "ChannelOrder", V, NULL ) || V < 0 || V > 5 )
			V = 0;
		Cached = V;
	}
	return Cached;
}

// Some Amiga SDL 1.2/CyberGraphX combinations report an RGB565 surface but
// scan its five-bit end channels in the opposite order.  Keep this separate
// from the 32-bit ChannelOrder setting: changing the software viewport between
// 16 and 32 bits must not break the already-correct 32-bit output.
static int SDLC_SwapRedBlue16()
{
	static int Cached = -1;
	if( Cached < 0 )
	{
		int V = 0;
		if( !GetConfigInt( "NSDLDrv.NSDLClient", "SwapRedBlue16", V, NULL ) )
			V = 0;
		Cached = V ? 1 : 0;
	}
	return Cached;
}

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
	// Note: the software path deliberately adds nothing here -- SDL_SWSURFACE
	// is 0, so it is the default. What matters is that SDL_OPENGL is NOT set;
	// a GL surface has ->pixels == NULL and cannot be blitted into.
	if( Flags & SDL_WINDOW_OPENGL )
		Out |= SDL_OPENGL;
	if( Flags & SDL_WINDOW_BORDERLESS )
		Out |= SDL_NOFRAME;
	return Out;
}

// SDL 1.2 selects the display pixel format while creating the video surface.
//
// SoftDrv always uses RGB565 here. 16bpp halves the framebuffer -- 600K
// instead of 1.2M at 640x480 -- and
// halves the bytes pushed through the blit each frame, which matters a lot on
// 68k.  SoftDrv supports 15/16/32bpp, but NOT 8: it has no palette path.
static int SDLC_VideoBpp( Uint32 WindowFlags = SDLC_WindowFlags )
{
	int Bpp = 0;

	// Preserve the established GL build behaviour: its AmigaMesa context uses
	// the actual display depth.
	if( WindowFlags & SDL_WINDOW_OPENGL )
	{
		const SDL_VideoInfo* Info = SDL_GetVideoInfo();
		Bpp = ( Info && Info->vfmt && Info->vfmt->BitsPerPixel )
			? Info->vfmt->BitsPerPixel
			: 16;
	}
	else
	{
		Bpp = 16;
	}
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] SDL12: video depth = %d bpp (%s)", Bpp,
		(WindowFlags & SDL_WINDOW_OPENGL) ? "OpenGL" : "Software" );
#endif
	return Bpp;
}

SDL_Window* SDL_CreateWindow( const char* Title, int X, int Y, int W, int H, Uint32 Flags )
{
	(void)X; (void)Y;
	if( SDLC_TheWindow )
	{
#ifdef PLATFORM_AMIGA
		AmigaDebugLogf( "[Amiga] SDL12: reusing early window %dx%d for %dx%d", SDLC_WindowW, SDLC_WindowH, W, H );
#endif
		// The early Amiga window may already be a fullscreen public screen.
		// Preserve that state while the viewport adopts the implicit SDL 1.2
		// window; otherwise the later resize needlessly switches it to a
		// window first, which fails on some VideoCore/P96 configurations.
		SDLC_WindowFlags = Flags | (SDLC_WindowFlags & SDL_WINDOW_FULLSCREEN);
		if( Title )
			SDL_WM_SetCaption( Title, Title );
		return SDLC_TheWindow;
	}

	const int Bpp = SDLC_VideoBpp( Flags );
	const Uint32 VideoFlags = SDLC_VideoFlags( Flags );
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] SDL12: SDL_SetVideoMode %dx%dx%d flags=0x%08lx", W, H, Bpp, (unsigned long)VideoFlags );
#endif
	SDL_Surface* Surface = SDL_SetVideoMode( W, H, Bpp, VideoFlags );
#ifdef PLATFORM_AMIGA
	SDLC_LogVideoMode( "CreateWindow", W, H, Bpp, VideoFlags, Surface );
#endif
	if( !Surface )
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
	const int Bpp = SDLC_VideoBpp( SDLC_WindowFlags );
	const Uint32 Flags = SDLC_VideoFlags( SDLC_WindowFlags );
	SDL_Surface* Surface = SDL_SetVideoMode( W, H, Bpp, Flags );
#ifdef PLATFORM_AMIGA
	SDLC_LogVideoMode( "SetWindowSize", W, H, Bpp, Flags, Surface );
#endif
	if( Surface )
	{
		SDLC_WindowW = W;
		SDLC_WindowH = H;
	}
}

int SDL_SetWindowFullscreen( SDL_Window* Win, Uint32 Flags )
{
	(void)Win;
	const Uint32 OldFlags = SDLC_WindowFlags;
	if( Flags & SDL_WINDOW_FULLSCREEN )
		SDLC_WindowFlags |= SDL_WINDOW_FULLSCREEN;
	else
		SDLC_WindowFlags &= ~SDL_WINDOW_FULLSCREEN;

	const int Bpp = SDLC_VideoBpp( SDLC_WindowFlags );
	const Uint32 VideoFlags = SDLC_VideoFlags( SDLC_WindowFlags );
	SDL_Surface* Surface = SDL_SetVideoMode( SDLC_WindowW, SDLC_WindowH, Bpp, VideoFlags );
#ifdef PLATFORM_AMIGA
	SDLC_LogVideoMode(
		(Flags & SDL_WINDOW_FULLSCREEN) ? "EnterFullscreen" : "LeaveFullscreen",
		SDLC_WindowW, SDLC_WindowH, Bpp, VideoFlags, Surface );
#endif
	if( Surface )
	{
		// SDL 1.2 may return a valid window surface after silently dropping
		// SDL_FULLSCREEN.  Report that as failure instead of letting the
		// engine believe it owns a fullscreen viewport.
		if( (Flags & SDL_WINDOW_FULLSCREEN) && !(Surface->flags & SDL_FULLSCREEN) )
		{
			SDLC_WindowFlags &= ~SDL_WINDOW_FULLSCREEN;
			return -1;
		}
		return 0;
	}

	SDLC_WindowFlags = OldFlags;
	return -1;
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

static const int SDLC_AnyModeResolutions[][2] =
{
	{ 320,  240 },
	{ 400,  300 },
	{ 512,  384 },
	{ 640,  480 },
	{ 800,  600 },
	{ 1024, 768 },
	{ 1152, 864 },
	{ 1280, 720 },
	{ 1280, 960 },
	{ 1280, 1024 },
	{ 1600, 900 },
	{ 1600, 1200 },
	{ 1920, 1080 }
};

int SDL_GetNumDisplayModes( int DisplayIndex )
{
	(void)DisplayIndex;
	SDL_Rect** Modes = SDLC_ListModes();
	// NULL: no modes. (SDL_Rect**)-1: SDL accepts any dimensions.
	if( !Modes )
		return 0;
	if( Modes == (SDL_Rect**)-1 )
		return sizeof(SDLC_AnyModeResolutions) / sizeof(SDLC_AnyModeResolutions[0]);

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
	if( !Modes )
		return -1;
	if( Modes == (SDL_Rect**)-1 )
	{
		const int Count = sizeof(SDLC_AnyModeResolutions) / sizeof(SDLC_AnyModeResolutions[0]);
		if( ModeIndex < 0 || ModeIndex >= Count )
			return -1;
		memset( Mode, 0, sizeof(*Mode) );
		Mode->w = SDLC_AnyModeResolutions[ModeIndex][0];
		Mode->h = SDLC_AnyModeResolutions[ModeIndex][1];
		return 0;
	}

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

	// SoftDrv needs the exact framebuffer format. The established OpenGL
	// path, however, must retain its original neutral viewport capabilities:
	// advertising CC_RGB565 there changes UE1's lighting data path.
	if( SDLC_WindowFlags & SDL_WINDOW_OPENGL )
		Mode->format = 0;
	else
		Mode->format = SDL_PIXELFORMAT_RGB565;
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
#ifdef PLATFORM_AMIGA
	return "AmigaOS";
#else
	return "Linux/PPC";
#endif
}

int SDL_GetCPUCount( void )
{
	return 1;
}

char* SDL_GetBasePath( void )
{
	// SDL2 callers free this with SDL_free, so allocate the same way.
#ifdef PLATFORM_AMIGA
	// PROGDIR: is the AmigaOS path to the running executable's directory.
	const char* Base = "PROGDIR:";
#else
	// The PPC test is launched from the game's System directory.
	const char* Base = "./";
#endif
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
	Renderer / Texture. SoftDrv renders into a locked streaming texture, so
	unlike the rest of the drawing API these are backed for real rather than
	stubbed: a texture is a plain staging buffer in fast RAM, and RenderCopy
	converts it into whatever format SDL_SetVideoMode actually gave us.

	Rendering into our own buffer and converting once per frame is also the
	right shape for Amiga: the video surface may live in slow Chip RAM, where
	the renderer's many small read-modify-write accesses would crawl.
-----------------------------------------------------------------------------*/

struct SDL_Renderer
{
	SDL_Surface* Target;
};

struct SDL_Texture
{
	Uint32 Format;
	int    W, H;
	int    Pitch;
	void*  Pixels;
};

static SDL_Renderer SDLC_TheRenderer;

SDL_Renderer* SDL_CreateRenderer( SDL_Window* Win, int Index, Uint32 Flags )
{
	(void)Win; (void)Index; (void)Flags;

	// SDL_SetVideoMode (done in SDL_CreateWindow) owns the only surface we
	// can present to; without it there is nothing to render into.
	SDL_Surface* Target = SDL_GetVideoSurface();
	if( !Target )
		return NULL;

	SDLC_TheRenderer.Target = Target;
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] SDL12: renderer on %dx%dx%d surface",
		Target->w, Target->h, Target->format ? Target->format->BitsPerPixel : 0 );
#endif
	return &SDLC_TheRenderer;
}

void SDL_DestroyRenderer( SDL_Renderer* Ren )
{
	if( Ren )
		Ren->Target = NULL;
}

SDL_Texture* SDL_CreateTexture( SDL_Renderer* Ren, Uint32 Format, int Access, int W, int H )
{
	(void)Access;
	if( !Ren || W <= 0 || H <= 0 )
		return NULL;

	SDL_Texture* Tex = (SDL_Texture*)calloc( 1, sizeof(SDL_Texture) );
	if( !Tex )
		return NULL;

	Tex->Format = Format;
	Tex->W      = W;
	Tex->H      = H;
	Tex->Pitch  = W * SDL_BYTESPERPIXEL( Format );
	Tex->Pixels = calloc( 1, (size_t)Tex->Pitch * (size_t)H );
	if( !Tex->Pixels )
	{
		free( Tex );
		return NULL;
	}

#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] SDL12: texture %dx%d fmt=0x%08lx pitch=%d bytes=%lu at %p",
		W, H, (unsigned long)Format, Tex->Pitch,
		(unsigned long)( (size_t)Tex->Pitch * (size_t)H ), Tex->Pixels );
#endif
	return Tex;
}

void SDL_DestroyTexture( SDL_Texture* Tex )
{
	if( Tex )
	{
		free( Tex->Pixels );
		free( Tex );
	}
}

int SDL_LockTexture( SDL_Texture* Tex, const SDL_Rect* Rect, void** Pixels, int* Pitch )
{
	// The engine only ever locks the whole texture.
	(void)Rect;
	if( !Tex )
	{
		if( Pixels ) *Pixels = NULL;
		if( Pitch )  *Pitch  = 0;
		return -1;
	}
	if( Pixels ) *Pixels = Tex->Pixels;
	if( Pitch )  *Pitch  = Tex->Pitch;
	return 0;
}

void SDL_UnlockTexture( SDL_Texture* Tex )
{
	(void)Tex;
}

// Convert one row of the staging texture into the video surface's format.
static void SDLC_ConvertRow( const SDL_PixelFormat* Fmt, Uint32 SrcFormat,
	const void* SrcRow, void* DstRow, int Count )
{
	const int DstBpp = Fmt->BytesPerPixel;

	// Fast path: identical layout, just copy. Covers both 565 and 555, since
	// the texture format now tracks the surface's real masks.
	if( DstBpp == 2
		&& !SDLC_SwapRedBlue16()
		&& ( ( SrcFormat == SDL_PIXELFORMAT_RGB565
				&& Fmt->Rmask == 0xF800 && Fmt->Gmask == 0x07E0 && Fmt->Bmask == 0x001F )
			|| ( SrcFormat == SDL_PIXELFORMAT_RGB555
				&& Fmt->Rmask == 0x7C00 && Fmt->Gmask == 0x03E0 && Fmt->Bmask == 0x001F ) ) )
	{
		memcpy( DstRow, SrcRow, (size_t)Count * 2 );
		return;
	}

	if( SrcFormat == SDL_PIXELFORMAT_ARGB8888 )
	{
		// SoftDrv writes 32bpp pixels bytewise: [+0]=B [+1]=G [+2]=R [+3]=A.
		// On x86 that is exactly ARGB8888's byte order, so the original code
		// could memcpy. On big-endian 68k the surface reads [+1]=R [+2]=G
		// [+3]=B, so a straight copy feeds it G where R belongs -- red flags
		// came out green. Read by byte offset and let SDL_MapRGB place the
		// channels according to the surface's own shifts.
		const Uint8* S = (const Uint8*)SrcRow;
		for( int i = 0; i < Count; i++ )
		{
			const Uint8 B = S[i*4+0], G = S[i*4+1], R = S[i*4+2];
			const Uint32 Out = SDL_MapRGB( (SDL_PixelFormat*)Fmt, R, G, B );
			if( DstBpp == 2 )      ((Uint16*)DstRow)[i] = (Uint16)Out;
			else if( DstBpp == 4 ) ((Uint32*)DstRow)[i] = Out;
			else                   ((Uint8*)DstRow)[i]  = (Uint8)Out;
		}
		return;
	}

	// 16-bit source into a surface with a different layout. Unpack according
	// to the SOURCE format -- treating a 555 source as 565 would shift red
	// and green by one bit each.
	const int Src555 = ( SrcFormat == SDL_PIXELFORMAT_RGB555 );
	const Uint16* Src = (const Uint16*)SrcRow;
	for( int i = 0; i < Count; i++ )
	{
		const Uint16 P = Src[i];
		Uint8 R = Src555
			? (Uint8)( ( ( P >> 10 ) & 0x1F ) * 255 / 31 )
			: (Uint8)( ( ( P >> 11 ) & 0x1F ) * 255 / 31 );
		const Uint8 G = Src555
			? (Uint8)( ( ( P >>  5 ) & 0x1F ) * 255 / 31 )
			: (Uint8)( ( ( P >>  5 ) & 0x3F ) * 255 / 63 );
		Uint8 B = (Uint8)( (   P         & 0x1F ) * 255 / 31 );
		if( SDLC_SwapRedBlue16() )
		{
			const Uint8 T = R;
			R = B;
			B = T;
		}
		const Uint32 Out = SDL_MapRGB( (SDL_PixelFormat*)Fmt, R, G, B );
		if( DstBpp == 2 )      ((Uint16*)DstRow)[i] = (Uint16)Out;
		else if( DstBpp == 4 ) ((Uint32*)DstRow)[i] = Out;
		else                   ((Uint8*)DstRow)[i]  = (Uint8)Out;
	}
}

int SDL_RenderCopy( SDL_Renderer* Ren, SDL_Texture* Tex, const SDL_Rect* Src, const SDL_Rect* Dst )
{
	// The engine always copies the whole texture over the whole window, and
	// the texture is recreated on resize, so no scaling is needed here.
	(void)Src; (void)Dst;

#ifdef PLATFORM_AMIGA
	{
		// Log entry BEFORE any early return, into the engine log (which is
		// known to work), so a bailout is distinguishable from never being
		// called at all.
		static int EntryLogged = 0;
		if( !EntryLogged )
		{
			EntryLogged = 1;
			char B[160];
			snprintf( B, sizeof(B), "[Amiga] RenderCopy entry: Ren=%p Target=%p Tex=%p Pixels=%p",
				(void*)Ren, (void*)( Ren ? Ren->Target : 0 ), (void*)Tex,
				(void*)( Tex ? Tex->Pixels : 0 ) );
			SDLC_EngineLog( B );
		}
	}
#endif

	if( !Ren || !Ren->Target || !Tex || !Tex->Pixels )
		return -1;

	// Re-fetch rather than trusting the cached pointer: SDL_SetVideoMode (via
	// SDL_SetWindowSize) replaces the video surface behind our back.
	SDL_Surface* Surf = SDL_GetVideoSurface();
	if( !Surf || !Surf->format )
		return -1;
	Ren->Target = Surf;

#ifdef PLATFORM_AMIGA
	{
		static int Once = 0;
		if( !Once )
		{
			Once = 1;
			AmigaDebugLogf( "[Amiga] SDL12: blit surf %dx%d pitch=%d bpp=%d pixels=%p mustlock=%d "
				"sflags=0x%08lx masks=%08lx/%08lx/%08lx | tex %dx%d pitch=%d",
				Surf->w, Surf->h, (int)Surf->pitch, (int)Surf->format->BytesPerPixel,
				Surf->pixels, (int)( SDL_MUSTLOCK( Surf ) ? 1 : 0 ),
				(unsigned long)Surf->flags,
				(unsigned long)Surf->format->Rmask, (unsigned long)Surf->format->Gmask,
				(unsigned long)Surf->format->Bmask, Tex->W, Tex->H, Tex->Pitch );
		}
	}
#endif

	// Lock if SDL says we must -- but also lock when ->pixels is NULL anyway,
	// which the Amiga backend does for double-buffered/hardware surfaces even
	// though SDL_MUSTLOCK() reports false.
	int Locked = 0;
	if( SDL_MUSTLOCK( Surf ) || !Surf->pixels )
	{
		if( SDL_LockSurface( Surf ) != 0 )
			return -1;
		Locked = 1;
	}

	if( !Surf->pixels )
	{
#ifdef PLATFORM_AMIGA
		static int NoPixLogged = 0;
		if( !NoPixLogged )
		{
			NoPixLogged = 1;
			SDLC_EngineLog( "[Amiga] RenderCopy: surface pixels still NULL after lock -- nothing blitted" );
		}
#endif
		if( Locked )
			SDL_UnlockSurface( Surf );
		return -1;
	}

#ifdef PLATFORM_AMIGA
	{
		// One-shot dump of the middle scanline: tells us whether the colours
		// are already wrong in the renderer's buffer or only after conversion.
		static int DumpDone = 0;
		if( !DumpDone && Tex->H > 8 )
		{
			DumpDone = 1;
			char Buf[256];
			const Uint8* Mid = (const Uint8*)Tex->Pixels + (size_t)( Tex->H / 2 ) * (size_t)Tex->Pitch;
			if( SDL_BYTESPERPIXEL( Tex->Format ) == 2 )
			{
				const Uint16* P = (const Uint16*)Mid;
				snprintf( Buf, sizeof(Buf),
					"[Amiga] SRC16 texfmt=0x%08lx: %04X %04X %04X %04X %04X %04X",
					(unsigned long)Tex->Format, P[Tex->W/2], P[Tex->W/2+1], P[Tex->W/2+2],
					P[Tex->W/2+3], P[Tex->W/2+4], P[Tex->W/2+5] );
			}
			else
			{
				const Uint32* P = (const Uint32*)Mid;
				snprintf( Buf, sizeof(Buf),
					"[Amiga] SRC32 texfmt=0x%08lx: %08lX %08lX %08lX %08lX",
					(unsigned long)Tex->Format, (unsigned long)P[Tex->W/2],
					(unsigned long)P[Tex->W/2+1], (unsigned long)P[Tex->W/2+2],
					(unsigned long)P[Tex->W/2+3] );
			}
			AmigaDebugLogf( "%s", Buf );
			SDLC_EngineLog( Buf );

			// Find the BRIGHTEST pixel in this row: a mid-screen sample is
			// usually dark wall where all three channels look alike, which
			// tells us nothing about channel order.
			if( SDL_BYTESPERPIXEL( Tex->Format ) == 4 )
			{
				const Uint8* S = (const Uint8*)Mid;
				int best = 0, bi = 0;
				for( int i = 0; i < Tex->W; i++ )
				{
					int s = S[i*4+0] + S[i*4+1] + S[i*4+2] + S[i*4+3];
					if( s > best ) { best = s; bi = i; }
				}
				snprintf( Buf, sizeof(Buf),
					"[Amiga] BRIGHTEST x=%d bytes +0=%02X +1=%02X +2=%02X +3=%02X",
					bi, S[bi*4+0], S[bi*4+1], S[bi*4+2], S[bi*4+3] );
				SDLC_EngineLog( Buf );
			}

			// The HARDWARE screen format, which can differ from the drawing
			// surface's: SDL 1.2 may hand back a surface in one layout while
			// CyberGraphX scans out another.
			{
				const SDL_VideoInfo* VI = SDL_GetVideoInfo();
				if( VI && VI->vfmt )
				{
					snprintf( Buf, sizeof(Buf),
						"[Amiga] SCREENFMT bpp=%d masks=%08lX/%08lX/%08lX shifts=%d/%d/%d loss=%d/%d/%d",
						(int)VI->vfmt->BytesPerPixel,
						(unsigned long)VI->vfmt->Rmask, (unsigned long)VI->vfmt->Gmask,
						(unsigned long)VI->vfmt->Bmask,
						(int)VI->vfmt->Rshift, (int)VI->vfmt->Gshift, (int)VI->vfmt->Bshift,
						(int)VI->vfmt->Rloss, (int)VI->vfmt->Gloss, (int)VI->vfmt->Bloss );
					SDLC_EngineLog( Buf );
				}
				snprintf( Buf, sizeof(Buf),
					"[Amiga] SURFFMT Amask=%08lX Ashift=%d flags=0x%08lX",
					(unsigned long)Surf->format->Amask, (int)Surf->format->Ashift,
					(unsigned long)Surf->flags );
				SDLC_EngineLog( Buf );
			}

			snprintf( Buf, sizeof(Buf),
				"[Amiga] DSTFMT bpp=%d masks=%08lX/%08lX/%08lX shifts=%d/%d/%d",
				(int)Surf->format->BytesPerPixel,
				(unsigned long)Surf->format->Rmask, (unsigned long)Surf->format->Gmask,
				(unsigned long)Surf->format->Bmask,
				(int)Surf->format->Rshift, (int)Surf->format->Gshift, (int)Surf->format->Bshift );
			AmigaDebugLogf( "%s", Buf );
			SDLC_EngineLog( Buf );
		}
	}
#endif

	const int Rows = ( Tex->H < Surf->h ) ? Tex->H : Surf->h;
	int Cols = ( Tex->W < Surf->w ) ? Tex->W : Surf->w;

	// Never write past the end of either row.
	const int SurfMax = (int)Surf->pitch / ( Surf->format->BytesPerPixel ? Surf->format->BytesPerPixel : 1 );
	if( Cols > SurfMax )
		Cols = SurfMax;

	for( int y = 0; y < Rows; y++ )
	{
		SDLC_ConvertRow( Surf->format, Tex->Format,
			(const Uint8*)Tex->Pixels + (size_t)y * (size_t)Tex->Pitch,
			(Uint8*)Surf->pixels + (size_t)y * (size_t)Surf->pitch,
			Cols );
	}

	if( Locked )
		SDL_UnlockSurface( Surf );

#ifdef PLATFORM_AMIGA
	{
		static int OnceDone = 0;
		if( !OnceDone ) { OnceDone = 1; AmigaDebugLogf( "[Amiga] SDL12: blit rows=%d cols=%d pixels=%p locked=%d OK",
			Rows, Cols, Surf->pixels, Locked ); }
	}
#endif
	return 0;
}

void SDL_RenderPresent( SDL_Renderer* Ren )
{
	(void)Ren;
	SDL_Surface* Surf = SDL_GetVideoSurface();
	if( !Surf )
		return;
#ifdef PLATFORM_AMIGA
	{
		static int Once = 0;
		if( !Once ) { Once = 1; AmigaDebugLogf( "[Amiga] SDL12: before SDL_Flip surf=%p", (void*)Surf ); }
	}
#endif
	SDL_Flip( Surf );
#ifdef PLATFORM_AMIGA
	{
		static int OnceD = 0;
		if( !OnceD ) { OnceD = 1; AmigaDebugLogf( "[Amiga] SDL12: after SDL_Flip" ); }
	}
#endif
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
