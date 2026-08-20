/*=============================================================================
	SDL12Compat.h: SDL2-on-SDL1.2 compatibility shim.

	Amiga 68k has SDL 1.2 only, but the engine is written against SDL2. This
	header maps the SDL2 subset the engine actually uses onto SDL 1.2, so the
	drivers can be built unmodified.

	SDL 1.2 has a single implicit video surface rather than window objects, so
	SDL_Window is a dummy type and the "current window" is always the surface
	returned by SDL_SetVideoMode.
=============================================================================*/

#ifndef _SDL12COMPAT_H_
#define _SDL12COMPAT_H_

// Amiga's SDL 1.2 config includes <exec/types.h>, which typedefs BYTE (as
// *signed* char) and WORD, colliding with the engine's own unsigned BYTE/WORD.
// The engine code that includes SDL never calls the Amiga OS directly, so
// block exec/types.h here by claiming its include guard, then provide the few
// OS types SDL's headers actually reference. This keeps the engine's types
// authoritative without touching driver sources.
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef void*          APTR;
typedef long           LONG;
typedef unsigned long  ULONG;
typedef unsigned short UWORD;
typedef unsigned char  UBYTE;
typedef short          BOOL;
typedef char*          STRPTR;
#ifndef NULL
#define NULL 0
#endif
#ifndef TRUE
#define TRUE  1
#define FALSE 0
#endif
#endif // EXEC_TYPES_H

#include <SDL.h>
#include <string.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

/*-----------------------------------------------------------------------------
	Window objects.
	SDL 1.2 has one implicit window; we hand out an opaque non-NULL handle.
-----------------------------------------------------------------------------*/

typedef struct SDL_Window SDL_Window;
typedef void* SDL_GLContext;

#define SDL_WINDOWPOS_UNDEFINED 0

#define SDL_WINDOW_FULLSCREEN 0x00000001
#define SDL_WINDOW_OPENGL     0x00000002
#define SDL_WINDOW_SHOWN      0x00000004
#define SDL_WINDOW_HIDDEN     0x00000008
#define SDL_WINDOW_BORDERLESS 0x00000010

extern SDL_Window* SDLC_TheWindow;
extern Uint32      SDLC_WindowFlags;

SDL_Window* SDL_CreateWindow( const char* Title, int X, int Y, int W, int H, Uint32 Flags );
void        SDL_DestroyWindow( SDL_Window* Win );
void        SDL_SetWindowTitle( SDL_Window* Win, const char* Title );
void        SDL_SetWindowSize( SDL_Window* Win, int W, int H );
int         SDL_SetWindowFullscreen( SDL_Window* Win, Uint32 Flags );
Uint32      SDL_GetWindowFlags( SDL_Window* Win );
void        SDL_ShowWindow( SDL_Window* Win );
void        SDL_GetWindowSize( SDL_Window* Win, int* W, int* H );
int         SDL_SetWindowBrightness( SDL_Window* Win, float Brightness );
int         SDL_GetWindowDisplayIndex( SDL_Window* Win );
SDL_Window* SDL_GetKeyboardFocus( void );
SDL_Window* SDL_GetMouseFocus( void );

/* No modal/parent window concept in SDL 1.2. */
#define SDL_SetWindowModalFor( w, p ) (0)

/*-----------------------------------------------------------------------------
	GL context. SDL 1.2 creates the context inside SDL_SetVideoMode.
-----------------------------------------------------------------------------*/

#define SDL_GL_CONTEXT_MAJOR_VERSION        17
#define SDL_GL_CONTEXT_MINOR_VERSION        18
#define SDL_GL_CONTEXT_PROFILE_MASK         21
#define SDL_GL_CONTEXT_PROFILE_COMPATIBILITY 0x0002
#define SDL_GL_CONTEXT_PROFILE_ES           0x0004

typedef int SDL_GLprofile;

SDL_GLContext SDL_GL_CreateContext( SDL_Window* Win );
void          SDL_GL_DeleteContext( SDL_GLContext Ctx );
int           SDL_GL_MakeCurrent( SDL_Window* Win, SDL_GLContext Ctx );
void          SDL_GL_SwapWindow( SDL_Window* Win );
int           SDL_GL_SetSwapInterval( int Interval );

/*-----------------------------------------------------------------------------
	Display modes.
-----------------------------------------------------------------------------*/

typedef struct SDL_DisplayMode
{
	Uint32 format;
	int w, h;
	int refresh_rate;
	void* driverdata;
} SDL_DisplayMode;

int SDL_GetNumDisplayModes( int DisplayIndex );
int SDL_GetDisplayMode( int DisplayIndex, int ModeIndex, SDL_DisplayMode* Mode );
int SDL_GetDesktopDisplayMode( int DisplayIndex, SDL_DisplayMode* Mode );
int SDL_GetWindowDisplayMode( SDL_Window* Win, SDL_DisplayMode* Mode );

/*-----------------------------------------------------------------------------
	Timing. SDL 1.2 has no performance counter; use SDL_GetTicks (1ms).
-----------------------------------------------------------------------------*/

Uint64 SDL_GetPerformanceCounter( void );
Uint64 SDL_GetPerformanceFrequency( void );

/*-----------------------------------------------------------------------------
	Misc system queries.
-----------------------------------------------------------------------------*/

const char* SDL_GetPlatform( void );
int         SDL_GetCPUCount( void );
char*       SDL_GetBasePath( void );

int   SDL_SetClipboardText( const char* Text );
char* SDL_GetClipboardText( void );
SDL_bool SDL_HasClipboardText( void );

int SDL_SetRelativeMouseMode( SDL_bool Enabled );
SDL_bool SDL_GetRelativeMouseMode( void );

/* Text input is always on in SDL 1.2 (unicode translation). */
void SDL_StartTextInput( void );

/* Hints do not exist in SDL 1.2. */
#define SDL_HINT_RENDER_SCALE_QUALITY "SDL_RENDER_SCALE_QUALITY"
#define SDL_SetHint( a, b ) (SDL_FALSE)

/*-----------------------------------------------------------------------------
	Message boxes. SDL 1.2 has none; fall back to stderr.
-----------------------------------------------------------------------------*/

#define SDL_MESSAGEBOX_ERROR       0x00000010
#define SDL_MESSAGEBOX_WARNING     0x00000020
#define SDL_MESSAGEBOX_INFORMATION 0x00000040

#define SDL_MESSAGEBOX_BUTTON_RETURNKEY_DEFAULT 0x00000001
#define SDL_MESSAGEBOX_BUTTON_ESCAPEKEY_DEFAULT 0x00000002
#define SDL_MESSAGEBOX_BUTTONS_LEFT_TO_RIGHT    0x00000800

typedef struct SDL_MessageBoxButtonData
{
	Uint32 flags;
	int buttonid;
	const char* text;
} SDL_MessageBoxButtonData;

typedef struct SDL_MessageBoxData
{
	Uint32 flags;
	SDL_Window* window;
	const char* title;
	const char* message;
	int numbuttons;
	const SDL_MessageBoxButtonData* buttons;
	const void* colorScheme;
} SDL_MessageBoxData;

int SDL_ShowSimpleMessageBox( Uint32 Flags, const char* Title, const char* Message, SDL_Window* Win );
int SDL_ShowMessageBox( const SDL_MessageBoxData* Data, int* ButtonId );

/*-----------------------------------------------------------------------------
	Game controllers: not supported on 68k. Stubbed out.
-----------------------------------------------------------------------------*/

#define SDL_INIT_GAMECONTROLLER 0

typedef struct SDL_GameController SDL_GameController;
typedef int SDL_GameControllerAxis;
typedef int SDL_GameControllerButton;

#define SDL_CONTROLLER_AXIS_MAX   6
#define SDL_CONTROLLER_BUTTON_MAX 15

#define SDL_GameControllerOpen( i )        ((SDL_GameController*)NULL)
#define SDL_GameControllerClose( c )       ((void)0)
#define SDL_GameControllerEventState( s )  (0)

/*-----------------------------------------------------------------------------
	Events that do not exist in SDL 1.2. Values must not collide with the
	SDL 1.2 SDL_EventType enum, which ends well below 64.
-----------------------------------------------------------------------------*/

#define SDL_MOUSEWHEEL            64
#define SDL_TEXTINPUT             65
#define SDL_CONTROLLERAXISMOTION  66
#define SDL_CONTROLLERBUTTONDOWN  67
#define SDL_CONTROLLERBUTTONUP    68

/*-----------------------------------------------------------------------------
	Scancodes. SDL 1.2 has no scancode layer, so map onto SDLK_* keysyms.
-----------------------------------------------------------------------------*/

typedef int SDL_Scancode;

#define SDL_NUM_SCANCODES SDLK_LAST

#define SDL_SCANCODE_A            SDLK_a
#define SDL_SCANCODE_Z            SDLK_z
#define SDL_SCANCODE_ESCAPE       SDLK_ESCAPE
#define SDL_SCANCODE_RETURN       SDLK_RETURN
#define SDL_SCANCODE_TAB          SDLK_TAB
#define SDL_SCANCODE_SPACE        SDLK_SPACE
#define SDL_SCANCODE_BACKSPACE    SDLK_BACKSPACE
#define SDL_SCANCODE_DELETE       SDLK_DELETE
#define SDL_SCANCODE_INSERT       SDLK_INSERT
#define SDL_SCANCODE_HOME         SDLK_HOME
#define SDL_SCANCODE_END          SDLK_END
#define SDL_SCANCODE_PAGEUP       SDLK_PAGEUP
#define SDL_SCANCODE_PAGEDOWN     SDLK_PAGEDOWN
#define SDL_SCANCODE_UP           SDLK_UP
#define SDL_SCANCODE_DOWN         SDLK_DOWN
#define SDL_SCANCODE_LEFT         SDLK_LEFT
#define SDL_SCANCODE_RIGHT        SDLK_RIGHT
#define SDL_SCANCODE_LSHIFT       SDLK_LSHIFT
#define SDL_SCANCODE_RSHIFT       SDLK_RSHIFT
#define SDL_SCANCODE_LCTRL        SDLK_LCTRL
#define SDL_SCANCODE_RCTRL        SDLK_RCTRL
#define SDL_SCANCODE_LALT         SDLK_LALT
#define SDL_SCANCODE_RALT         SDLK_RALT
#define SDL_SCANCODE_CAPSLOCK     SDLK_CAPSLOCK
#define SDL_SCANCODE_PRINTSCREEN  SDLK_PRINT
#define SDL_SCANCODE_GRAVE        SDLK_BACKQUOTE
#define SDL_SCANCODE_COMMA        SDLK_COMMA
#define SDL_SCANCODE_PERIOD       SDLK_PERIOD
#define SDL_SCANCODE_SLASH        SDLK_SLASH
#define SDL_SCANCODE_BACKSLASH    SDLK_BACKSLASH
#define SDL_SCANCODE_SEMICOLON    SDLK_SEMICOLON
#define SDL_SCANCODE_EQUALS       SDLK_EQUALS
#define SDL_SCANCODE_LEFTBRACKET  SDLK_LEFTBRACKET
#define SDL_SCANCODE_RIGHTBRACKET SDLK_RIGHTBRACKET
#define SDL_SCANCODE_KP_PERIOD    SDLK_KP_PERIOD

// Number rows and function keys. SDL 1.2 keysyms are contiguous across these
// ranges, matching how NSDLDrv fills KeyMap with INIT_KEY_RANGE.
#define SDL_SCANCODE_0            SDLK_0
#define SDL_SCANCODE_1            SDLK_1
#define SDL_SCANCODE_9            SDLK_9
#define SDL_SCANCODE_KP_0         SDLK_KP0
#define SDL_SCANCODE_KP_1         SDLK_KP1
#define SDL_SCANCODE_KP_9         SDLK_KP9
#define SDL_SCANCODE_F1           SDLK_F1
#define SDL_SCANCODE_F12          SDLK_F12
// SDL 1.2 only defines up to F15, and Amiga keyboards have no F13+. Map the
// F13..F24 range to the unused keysyms just past F15 so KeyMap stays in
// bounds (KeyMap is sized SDLK_LAST); these entries are simply never hit.
#define SDL_SCANCODE_F13          (SDLK_F15+1)
#define SDL_SCANCODE_F24          (SDLK_F15+12)

/*-----------------------------------------------------------------------------
	Renderer / Texture (SDL2 accelerated-blit path). Amiga always uses GL, so
	this path is never taken, but it must still compile. Provide the types and
	no-op stubs.
-----------------------------------------------------------------------------*/

typedef struct SDL_Renderer SDL_Renderer;
typedef struct SDL_Texture SDL_Texture;

#define SDL_RENDERER_SOFTWARE       0x00000001
#define SDL_TEXTUREACCESS_STREAMING 1

/* Pixel format helpers. ARGB8888 = 4 bytes/pixel, not 565. */
#define SDL_PIXELFORMAT_ARGB8888    0x16362004
#define SDL_PACKEDLAYOUT_565        1
#define SDL_BYTESPERPIXEL( fmt )    (((fmt) == SDL_PIXELFORMAT_ARGB8888) ? 4 : 2)
#define SDL_PIXELLAYOUT( fmt )      (0)

SDL_Renderer* SDL_CreateRenderer( SDL_Window* Win, int Index, Uint32 Flags );
void          SDL_DestroyRenderer( SDL_Renderer* Ren );
SDL_Texture*  SDL_CreateTexture( SDL_Renderer* Ren, Uint32 Format, int Access, int W, int H );
void          SDL_DestroyTexture( SDL_Texture* Tex );
int           SDL_LockTexture( SDL_Texture* Tex, const SDL_Rect* Rect, void** Pixels, int* Pitch );
void          SDL_UnlockTexture( SDL_Texture* Tex );
int           SDL_RenderCopy( SDL_Renderer* Ren, SDL_Texture* Tex, const SDL_Rect* Src, const SDL_Rect* Dst );
void          SDL_RenderPresent( SDL_Renderer* Ren );

#ifdef __cplusplus
}
#endif

#endif /* _SDL12COMPAT_H_ */
