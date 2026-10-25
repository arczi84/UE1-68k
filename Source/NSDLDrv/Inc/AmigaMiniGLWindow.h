#ifndef UE1_AMIGA_MINIGL_WINDOW_H
#define UE1_AMIGA_MINIGL_WINDOW_H

#ifdef __cplusplus
extern "C" {
#endif

typedef union SDL_Event SDL_Event;

int   AmigaMiniGLOpenWindow( int Width, int Height, int Fullscreen, int ColorBits );
const char* AmigaMiniGLGetOpenError( void );
void  AmigaMiniGLCloseWindow( void );
void* AmigaMiniGLGetWindow( void );
void  AmigaMiniGLSwapBuffers( void );
void  AmigaMiniGLSetFrameLock( int Enabled );
int   AmigaMiniGLBeginFrame( void );
void  AmigaMiniGLEndFrame( void );
void  AmigaMiniGLSetTitle( const char* Title );
void  AmigaMiniGLSetPointerVisible( int Visible );
int   AmigaMiniGLHasFocus( void );
int   AmigaMiniGLPollEvent( SDL_Event* Event );

#ifdef __cplusplus
}
#endif

#endif
