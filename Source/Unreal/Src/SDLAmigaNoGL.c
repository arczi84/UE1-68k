/* Optional GL hooks required by SDL's CyberGraphX video driver. The native
 * MiniGL build never asks SDL for an OpenGL mode, so these remain inert. */
/* This SDL_mixer archive was built against a libc exposing errno as a global;
 * libnix uses an accessor macro, so provide the ABI slot the archive expects. */
int errno;

int CGX_GL_Init( void* Device ) { (void)Device; return -1; }
void CGX_GL_Quit( void* Device ) { (void)Device; }
int CGX_GL_Update( void* Device ) { (void)Device; return -1; }
int CGX_GL_MakeCurrent( void* Device ) { (void)Device; return -1; }
int CGX_GL_GetAttribute( void* Device, int Attribute, int* Value )
{
    (void)Device; (void)Attribute; (void)Value; return -1;
}
void CGX_GL_SwapBuffers( void* Device ) { (void)Device; }
void* CGX_GL_GetProcAddress( void* Device, const char* Name )
{
    (void)Device; (void)Name; return 0;
}
int CGX_GL_LoadLibrary( void* Device, const char* Path )
{
    (void)Device; (void)Path; return -1;
}
