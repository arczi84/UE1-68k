/*
 * Native Intuition/MiniGL window for UE1. SDL remains initialized for input
 * devices and audio, but never owns the video mode or GL context.
 */
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include <exec/memory.h>
#include <exec/types.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
/* SDL's video backend closes and clears its own IntuitionBase on shutdown.
 * The native window must own an independent reference for its entire lifetime. */
#define INTUITION_BASE_NAME NativeIntuitionBase
static struct IntuitionBase* NativeIntuitionBase = NULL;
#include <proto/intuition.h>
#include <proto/keymap.h>

#include <SDL.h>
#include <proto/minigl.h>

#include "AmigaMiniGLWindow.h"

extern void AmigaMiniGLBufferProbeBefore(struct Window *window);
extern void AmigaMiniGLBufferProbeAfter(struct Window *window);
extern void AmigaMiniGLBufferProbeClose(void);
extern void AmigaRawMouseShutdown(void);
extern void AmigaEmergencyProcessExit(int result) __attribute__((noreturn));
#ifdef UE_AMIGA_GPROF
extern void _moncleanup(void);
#endif

#ifdef MGL_CAPTURE
extern void UETraceStart(int width, int height, int fullscreen);
extern void UETraceStop(void);
#endif

struct Library* KeymapBase = NULL;

static struct Window* NativeWindow = NULL;
static UWORD* BlankPointer = NULL;
static int MiniGLLibraryOpen = 0;
static void ReleaseDisplayLock( void );
static int FrameLockEnabled = -1;
static int NativeHasFocus = 0;
static int LastMouseX = 0;
static int LastMouseY = 0;
static Uint8 MouseButtons = 0;
/* SetWindowTitles() keeps this pointer; UpdateWindow() passes a stack buffer. */
static char NativeWindowTitle[128] = "Unreal";
static int PresentationProbe = -1;
static unsigned long ProbeFrames = 1;
static unsigned long ProbeSwaps = 1;
static char ProbeWindowTitle[128] = "Unreal probe";

void AmigaMiniGLProbePhase( const char* Phase )
{
    if( PresentationProbe != 1 || !NativeWindow )
        return;
    if( strcmp( Phase, "render-start" ) == 0 )
        ++ProbeFrames;
    if( strcmp( Phase, "swap-returned" ) == 0 )
        ++ProbeSwaps;
    snprintf( ProbeWindowTitle, sizeof(ProbeWindowTitle),
        "Unreal probe36: frame=%lu swap=%lu %s", ProbeFrames, ProbeSwaps, Phase );
    SetWindowTitles( NativeWindow, (STRPTR)ProbeWindowTitle, (STRPTR)-1 );
}

void AmigaMiniGLEnableProbe( int Enabled )
{
    PresentationProbe = Enabled;
    ProbeFrames = ProbeSwaps = 0;
    AmigaMiniGLProbePhase( "renderer-init" );
}

static SDLMod TranslateModifiers( UWORD Qualifier )
{
    unsigned int Mod = KMOD_NONE;
    if( Qualifier & IEQUALIFIER_LSHIFT )   Mod |= KMOD_LSHIFT;
    if( Qualifier & IEQUALIFIER_RSHIFT )   Mod |= KMOD_RSHIFT;
    if( Qualifier & IEQUALIFIER_CONTROL )  Mod |= KMOD_LCTRL;
    if( Qualifier & IEQUALIFIER_LALT )     Mod |= KMOD_LALT;
    if( Qualifier & IEQUALIFIER_RALT )     Mod |= KMOD_RALT;
    if( Qualifier & IEQUALIFIER_LCOMMAND ) Mod |= KMOD_LMETA;
    if( Qualifier & IEQUALIFIER_RCOMMAND ) Mod |= KMOD_RMETA;
    if( Qualifier & IEQUALIFIER_CAPSLOCK ) Mod |= KMOD_CAPS;
    if( Qualifier & IEQUALIFIER_NUMERICPAD ) Mod |= KMOD_NUM;
    return (SDLMod)Mod;
}

static SDLKey TranslateSpecialKey( UBYTE Raw )
{
    switch( Raw )
    {
        case 0x0f: return SDLK_KP0;
        case 0x1d: return SDLK_KP1;
        case 0x1e: return SDLK_KP2;
        case 0x1f: return SDLK_KP3;
        case 0x2d: return SDLK_KP4;
        case 0x2e: return SDLK_KP5;
        case 0x2f: return SDLK_KP6;
        case 0x3c: return SDLK_KP_PERIOD;
        case 0x3d: return SDLK_KP7;
        case 0x3e: return SDLK_KP8;
        case 0x3f: return SDLK_KP9;
        case 0x40: return SDLK_SPACE;
        case 0x41: return SDLK_BACKSPACE;
        case 0x42: return SDLK_TAB;
        case 0x43: return SDLK_KP_ENTER;
        case 0x44: return SDLK_RETURN;
        case 0x45: return SDLK_ESCAPE;
        case 0x46: return SDLK_DELETE;
        case 0x47: return SDLK_INSERT;
        case 0x48: return SDLK_PAGEUP;
        case 0x49: return SDLK_PAGEDOWN;
        case 0x4a: return SDLK_KP_MINUS;
        case 0x4b: return SDLK_F11;
        case 0x4c: return SDLK_UP;
        case 0x4d: return SDLK_DOWN;
        case 0x4e: return SDLK_RIGHT;
        case 0x4f: return SDLK_LEFT;
        case 0x50: return SDLK_F1;
        case 0x51: return SDLK_F2;
        case 0x52: return SDLK_F3;
        case 0x53: return SDLK_F4;
        case 0x54: return SDLK_F5;
        case 0x55: return SDLK_F6;
        case 0x56: return SDLK_F7;
        case 0x57: return SDLK_F8;
        case 0x58: return SDLK_F9;
        case 0x59: return SDLK_F10;
        case 0x5c: return SDLK_KP_DIVIDE;
        case 0x5d: return SDLK_KP_MULTIPLY;
        case 0x5e: return SDLK_KP_PLUS;
        case 0x5f: return SDLK_HELP;
        case 0x60: return SDLK_LSHIFT;
        case 0x61: return SDLK_RSHIFT;
        case 0x62: return SDLK_CAPSLOCK;
        case 0x63: return SDLK_LCTRL;
        case 0x64: return SDLK_LALT;
        case 0x65: return SDLK_RALT;
        case 0x66: return SDLK_LMETA;
        case 0x67: return SDLK_RMETA;
        case 0x6b: return SDLK_SCROLLOCK;
        case 0x6c: return SDLK_PRINT;
        case 0x6d: return SDLK_NUMLOCK;
        case 0x6e: return SDLK_PAUSE;
        case 0x6f: return SDLK_F12;
        case 0x70: return SDLK_HOME;
        case 0x71: return SDLK_END;
        default:   return SDLK_UNKNOWN;
    }
}

static Uint16 MapRawCharacter( UBYTE Raw, UWORD Qualifier, APTR Address )
{
    struct InputEvent Input;
    char Buffer[8];
    WORD Count;

    if( !KeymapBase )
        return 0;
    memset( &Input, 0, sizeof(Input) );
    Input.ie_Class = IECLASS_RAWKEY;
    Input.ie_Code = Raw;
    Input.ie_Qualifier = Qualifier;
    Input.ie_EventAddress = Address;
    Count = MapRawKey( &Input, Buffer, sizeof(Buffer), NULL );
    return Count > 0 ? (Uint16)(unsigned char)Buffer[0] : 0;
}

static SDLKey TranslateKey( UBYTE Raw, APTR Address )
{
    SDLKey Special = TranslateSpecialKey( Raw );
    Uint16 Character;
    if( Special != SDLK_UNKNOWN )
        return Special;
    Character = MapRawCharacter( Raw, 0, Address );
    return Character ? (SDLKey)Character : SDLK_UNKNOWN;
}

extern const char *UEMiniGLGetOpenError(void);
static char NativeOpenError[320] = "MiniGL window not opened";
const char *AmigaMiniGLGetOpenError(void) { return NativeOpenError; }

int AmigaMiniGLOpenWindow( int Width, int Height, int Fullscreen, int ColorBits )
{
    ULONG Events;
    if( ColorBits != 16 && ColorBits != 32 )
    {
        snprintf(NativeOpenError, sizeof(NativeOpenError), "unsupported depth %d", ColorBits);
        return 0;
    }
    if( NativeWindow )
        return 1;
    FrameLockEnabled = 0;
    NativeIntuitionBase = (struct IntuitionBase*)OpenLibrary("intuition.library", 39);
    if( !NativeIntuitionBase )
    {
        snprintf(NativeOpenError, sizeof(NativeOpenError), "OpenLibrary(intuition.library, 39) failed");
        return 0;
    }
    if( !MiniGLOpen() )
    {
        snprintf(NativeOpenError, sizeof(NativeOpenError), "%s", UEMiniGLGetOpenError());
        goto fail_intuition;
    }
    MiniGLLibraryOpen = 1;

    mglChooseWindowMode( Fullscreen ? GL_FALSE : GL_TRUE );
    mglChooseNumberOfBuffers( 2 );
    mglChoosePixelDepth( ColorBits );
    mglChooseVertexBufferSize( 4096 );
    AmigaMiniGLModeTrace( "open: before mglCreateContext %dx%d fs=%d", Width, Height, Fullscreen );
    if( !mglCreateContext( 0, 0, Width, Height ) )
    {
        AmigaMiniGLModeTrace( "open: mglCreateContext FAILED" );
        snprintf(NativeOpenError, sizeof(NativeOpenError),
            "mglCreateContext(%dx%d, depth=%d, fullscreen=%d) failed; library=%u.%u ABI=%lu size=%lu backend=%lu",
            Width, Height, ColorBits, Fullscreen, MiniGLBase->lib_Version, MiniGLBase->lib_Revision,
            MiniGLDispatch->abiVersion, MiniGLDispatch->structSize, MiniGLDispatch->backendFlags);
        MiniGLClose();
        MiniGLLibraryOpen = 0;
        goto fail_intuition;
    }

#ifdef MGL_CAPTURE
    UETraceStart(Width, Height, Fullscreen);
#endif
    AmigaMiniGLModeTrace( "open: context created" );
    mglLockMode( MGL_LOCK_SMART );

    NativeWindow = (struct Window*)mglGetInputWindowHandle();
    if( !NativeWindow )
        NativeWindow = (struct Window*)mglGetWindowHandle();
    if( !NativeWindow )
    {
        snprintf(NativeOpenError, sizeof(NativeOpenError), "MiniGL context created but window handles are NULL");
#ifdef MGL_CAPTURE
        UETraceStop();
#endif
        mglDeleteContext();
        MiniGLClose();
        MiniGLLibraryOpen = 0;
        goto fail_intuition;
    }

    Events = IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS | IDCMP_MOUSEMOVE |
        IDCMP_CLOSEWINDOW | IDCMP_ACTIVEWINDOW | IDCMP_INACTIVEWINDOW;
    ModifyIDCMP( NativeWindow, Events );
    ReportMouse( TRUE, NativeWindow );
    KeymapBase = OpenLibrary( "keymap.library", 0 );
    BlankPointer = (UWORD*)AllocMem( 16, MEMF_CHIP | MEMF_CLEAR );
    NativeHasFocus = 1;
    LastMouseX = NativeWindow->MouseX;
    LastMouseY = NativeWindow->MouseY;
    MouseButtons = 0;
    return 1;

fail_intuition:
    CloseLibrary((struct Library*)NativeIntuitionBase);
    NativeIntuitionBase = NULL;
    return 0;
}

void AmigaMiniGLCloseWindow( void )
{
    AmigaMiniGLEndFrame();
    FrameLockEnabled = 0;
    AmigaMiniGLBufferProbeClose();
#ifdef MGL_CAPTURE
    UETraceStop();
#endif
    NativeHasFocus = 0;
    ReleaseDisplayLock();
    if( NativeWindow )
    {
        ClearPointer( NativeWindow );
        ModifyIDCMP( NativeWindow, 0 );
    }
    if( BlankPointer )
    {
        FreeMem( BlankPointer, 16 );
        BlankPointer = NULL;
    }
    if( KeymapBase )
    {
        CloseLibrary( KeymapBase );
        KeymapBase = NULL;
    }
    if( MiniGLLibraryOpen )
    {
        AmigaMiniGLModeTrace( "close: before mglDeleteContext" );
        mglDeleteContext();
        AmigaMiniGLModeTrace( "close: after mglDeleteContext" );
        NativeWindow = NULL;
        MiniGLClose();
        AmigaMiniGLModeTrace( "close: library closed" );
        MiniGLLibraryOpen = 0;
    }
    if( NativeIntuitionBase )
    {
        CloseLibrary((struct Library*)NativeIntuitionBase);
        NativeIntuitionBase = NULL;
    }
}

void* AmigaMiniGLGetWindow( void )
{
    return NativeWindow;
}

void AmigaMiniGLSetFrameLock( int Enabled )
{
    FrameLockEnabled = Enabled ? 1 : 0;
    mglLockMode(Enabled ? MGL_LOCK_MANUAL : MGL_LOCK_SMART);
}

int AmigaMiniGLBeginFrame( void )
{
    if( FrameLockEnabled != 1 ) return 1;
    return NativeWindow && mglLockDisplay();
}

void AmigaMiniGLEndFrame( void )
{
    if( FrameLockEnabled == 1 && MiniGLLibraryOpen ) mglUnlockDisplay();
}

/* PiStorm3D's fullscreen mode keeps the screen bitmap locked from one
 * mglSwitchDisplay until the next frame's first draw, and CyberGraphX forbids
 * graphics/Intuition calls on a locked bitmap: SetPointer or SetWindowTitles
 * there hangs on the first frame. glFinish releases that lock (it waits for
 * the pending render), so call it before any Intuition call on NativeWindow.
 * These calls are rare, so per-frame CPU/GPU overlap is unaffected. */
static void ReleaseDisplayLock( void )
{
    if( NativeWindow && MiniGLLibraryOpen )
        glFinish();
}

/* Mode-switch trace for PiStorm3D grey-screen hangs. Opened and closed per
 * line so the file survives the reset a hang forces; Unreal.log does not. */
void AmigaMiniGLModeTrace( const char* Fmt, ... )
{
    FILE* F = fopen( "PROGDIR:modeswitch.log", "a" );
    va_list Args;
    if( !F )
        return;
    va_start( Args, Fmt );
    vfprintf( F, Fmt, Args );
    va_end( Args );
    fputc( '\n', F );
    fclose( F );
}

void AmigaMiniGLReleaseDisplayLock( void )
{
    ReleaseDisplayLock();
}

void AmigaMiniGLSwapBuffers( void )
{
    if( NativeWindow )
    {
        AmigaMiniGLProbePhase( "before-swap" );
        AmigaMiniGLBufferProbeBefore(NativeWindow);
        mglSwitchDisplay();
        AmigaMiniGLProbePhase( "swap-returned" );
        AmigaMiniGLBufferProbeAfter(NativeWindow);
    }
}

void AmigaMiniGLSetTitle( const char* Title )
{
    if( NativeWindow && Title )
    {
        strncpy( NativeWindowTitle, Title, sizeof(NativeWindowTitle) - 1 );
        NativeWindowTitle[sizeof(NativeWindowTitle) - 1] = '\0';
        ReleaseDisplayLock();
        SetWindowTitles( NativeWindow, (STRPTR)NativeWindowTitle, (STRPTR)-1 );
    }
}

void AmigaMiniGLSetPointerVisible( int Visible )
{
    if( !NativeWindow )
        return;
    ReleaseDisplayLock();
    if( Visible || !BlankPointer )
        ClearPointer( NativeWindow );
    else
        SetPointer( NativeWindow, BlankPointer, 1, 1, 0, 0 );
}

int AmigaMiniGLHasFocus( void )
{
    return NativeHasFocus;
}

int AmigaMiniGLPollEvent( SDL_Event* Event )
{
    struct IntuiMessage* Message;
    ULONG Class;
    UWORD Code, Qualifier;
    WORD MouseX, MouseY;
    APTR Address;

    if( !NativeWindow || !Event || !NativeWindow->UserPort )
        return 0;
    Message = (struct IntuiMessage*)GetMsg( NativeWindow->UserPort );
    if( !Message )
        return 0;

    Class = Message->Class;
    Code = Message->Code;
    Qualifier = Message->Qualifier;
    MouseX = Message->MouseX - NativeWindow->BorderLeft;
    MouseY = Message->MouseY - NativeWindow->BorderTop;
    Address = Message->IAddress;
    ReplyMsg( (struct Message*)Message );
    memset( Event, 0, sizeof(*Event) );

    switch( Class )
    {
        case IDCMP_CLOSEWINDOW:
            Event->type = SDL_QUIT;
            return 1;
        case IDCMP_ACTIVEWINDOW:
            NativeHasFocus = 1;
            return AmigaMiniGLPollEvent( Event );
        case IDCMP_INACTIVEWINDOW:
            NativeHasFocus = 0;
            return AmigaMiniGLPollEvent( Event );
        case IDCMP_RAWKEY:
        {
            UBYTE Raw = (UBYTE)(Code & 0x7f);
            int Released = (Code & IECODE_UP_PREFIX) != 0;
            if( Raw == 0x59 && !Released ) /* F10: bypass UE menu/GC/exit path. */
            {
#ifdef UE_AMIGA_GPROF
                /* Stop the interrupt sampler and save before closing GL. */
                _moncleanup();
#endif
                /* The IDCMP message has already been replied to above. Stop
                 * audio before graphics cleanup so it cannot keep playing. */
                SDL_PauseAudio(1);
                SDL_CloseAudio();
                AmigaRawMouseShutdown();
                AmigaMiniGLCloseWindow();
                SDL_Quit();
                AmigaEmergencyProcessExit(0);
            }
            Event->type = Released ? SDL_KEYUP : SDL_KEYDOWN;
            Event->key.which = 0;
            Event->key.state = Released ? SDL_RELEASED : SDL_PRESSED;
            Event->key.keysym.scancode = Raw;
            Event->key.keysym.sym = TranslateKey( Raw, Address );
            Event->key.keysym.mod = TranslateModifiers( Qualifier );
            Event->key.keysym.unicode = Released ? 0 :
                MapRawCharacter( Raw, Qualifier, Address );
            return 1;
        }
        case IDCMP_MOUSEBUTTONS:
            Event->type = (Code & IECODE_UP_PREFIX) ?
                SDL_MOUSEBUTTONUP : SDL_MOUSEBUTTONDOWN;
            Event->button.which = 0;
            Event->button.state = Event->type == SDL_MOUSEBUTTONDOWN ?
                SDL_PRESSED : SDL_RELEASED;
            switch( Code & ~IECODE_UP_PREFIX )
            {
                case IECODE_LBUTTON: Event->button.button = SDL_BUTTON_LEFT; break;
                case IECODE_MBUTTON: Event->button.button = SDL_BUTTON_MIDDLE; break;
                case IECODE_RBUTTON: Event->button.button = SDL_BUTTON_RIGHT; break;
                default: return AmigaMiniGLPollEvent( Event );
            }
            if( Event->type == SDL_MOUSEBUTTONDOWN )
                MouseButtons |= SDL_BUTTON( Event->button.button );
            else
                MouseButtons &= ~SDL_BUTTON( Event->button.button );
            Event->button.x = MouseX;
            Event->button.y = MouseY;
            return 1;
        case IDCMP_MOUSEMOVE:
            Event->type = SDL_MOUSEMOTION;
            Event->motion.which = 0;
            Event->motion.state = MouseButtons;
            Event->motion.x = MouseX;
            Event->motion.y = MouseY;
            Event->motion.xrel = MouseX - LastMouseX;
            Event->motion.yrel = MouseY - LastMouseY;
            LastMouseX = MouseX;
            LastMouseY = MouseY;
            return 1;
        default:
            return AmigaMiniGLPollEvent( Event );
    }
}
