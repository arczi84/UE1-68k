/*
 * Bridge SDL-mgl's legacy context entry points to the shared register-LVO
 * minigl.library v10 dispatch-table ABI used by the Amiga Dethrace port.
 *
 * SDL-mgl supplies the SDL 1.2 window/input integration, but was compiled
 * against the old static MiniGL API. The renderer itself and these wrappers
 * call the public shared-library stubs from libminigl_register.a.
 */
#include <proto/minigl.h>
#include <proto/dos.h>

#include <stddef.h>
#include <string.h>

GLcontext mini_CurrentContext = NULL;

GLboolean MGLInit(void)
{
    return MiniGLOpen() ? GL_TRUE : GL_FALSE;
}

void MGLTerm(void)
{
    /* Some v10 classic builds print an unconditional LibClose diagnostic.
     * Temporarily detach the process output so quitting a Workbench-launched
     * game does not open a Shell window just for that message. */
    BPTR oldOutput = SelectOutput((BPTR)0);
    MiniGLClose();
    SelectOutput(oldOutput);
}

void *MGLCreateContext(int offx, int offy, int width, int height)
{
    /* Match the stable Dethrace MiniGL setup.  UE1 can feed substantially
     * more vertices through MiniGL during a scene/HUD frame than the small
     * library default accommodates.  This must be selected before creating
     * the context because MiniGL allocates its vertex storage there. */
    mglChooseVertexBufferSize(4096);
    mini_CurrentContext = (GLcontext)mglCreateContext(offx, offy, width, height);
    return mini_CurrentContext;
}

void MGLDeleteContext(GLcontext context)
{
    (void)context;
    mglDeleteContext();
    mini_CurrentContext = NULL;
}

void *MGLGetWindowHandle(GLcontext context)
{
    (void)context;
    return mglGetWindowHandle();
}

void MGLLockMode(GLcontext context, GLenum mode)
{
    (void)context;
    mglLockMode(mode);
}

void MGLSwitchDisplay(GLcontext context)
{
    (void)context;
    mglSwitchDisplay();
}

/* SDL-mgl calls these legacy public symbols directly. MiniGL v10 exposes
 * them as dispatch-table macros, so provide the four tiny ABI adapters SDL
 * needs while keeping all rendering calls on the v10 table. */
#undef mglChoosePixelDepth
#undef mglChooseNumberOfBuffers
#undef mglChooseWindowMode
#undef mglProposeCloseDesktop

void mglChoosePixelDepth(int depth)
{
    /* The dispatch ABI is backend-neutral. PiStorm3D currently renders only
     * to 32-bit targets, whereas classic MiniGL must retain SDL's selected
     * screen depth (including the WinUAE 16-bit test setup). */
    if (MiniGLDispatch &&
        (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_PISTORM3D))
        depth = 32;
    MiniGLDispatch->mglChoosePixelDepth(depth);
}

void mglChooseNumberOfBuffers(int number)
{
    MiniGLDispatch->mglChooseNumberOfBuffers(number);
}

void mglChooseWindowMode(GLboolean flag)
{
    MiniGLDispatch->mglChooseWindowMode(flag);
}

void mglProposeCloseDesktop(GLboolean closeme)
{
    MiniGLDispatch->mglProposeCloseDesktop(closeme);
}

/* SDL-mgl resolves the complete OpenGL 1.1 table while it creates the video
 * mode, even though NMiniGLDrv calls MiniGL directly.  Its bundled resolver
 * targets the old static MiniGL internals, so expose the addressable v10
 * inline wrappers here instead. */
static void SDLCompat_glInterleavedArrays(GLenum format, GLsizei stride,
                                          const GLvoid *pointer)
{
    (void)format;
    (void)stride;
    (void)pointer;
}

static void SDLCompat_glPixelStoref(GLenum pname, GLfloat param)
{
    MGLD_glPixelStorei(pname, (GLint)param);
}

static void SDLCompat_glTexParameterfv(GLenum target, GLenum pname,
                                       const GLfloat *params)
{
    MGLD_glTexParameterf(target, pname, params ? *params : 0.0f);
}

static void SDLCompat_glTexParameteriv(GLenum target, GLenum pname,
                                       const GLint *params)
{
    MGLD_glTexParameteri(target, pname, params ? *params : 0);
}

static void SDLCompat_glVertex2i(GLint x, GLint y)
{
    MGLD_glVertex2f((GLfloat)x, (GLfloat)y);
}

void *AmiGetGLProc(const char *name)
{
#define MAP_GL(public_name) \
    if (strcmp(name, #public_name) == 0) return (void *)MGLD_##public_name
#define MAP_COMPAT(public_name) \
    if (strcmp(name, #public_name) == 0) return (void *)SDLCompat_##public_name

    if (!name || !MiniGLDispatch)
        return NULL;

    MAP_GL(glAlphaFunc);
    MAP_GL(glArrayElement);
    MAP_GL(glBegin);
    MAP_GL(glBindTexture);
    MAP_GL(glBlendFunc);
    MAP_GL(glClear);
    MAP_GL(glClearColor);
    MAP_GL(glClearDepth);
    MAP_GL(glColor3f);
    MAP_GL(glColor3fv);
    MAP_GL(glColor3ub);
    MAP_GL(glColor3ubv);
    MAP_GL(glColor4f);
    MAP_GL(glColor4fv);
    MAP_GL(glColor4ub);
    MAP_GL(glColor4ubv);
    MAP_GL(glColorPointer);
    MAP_GL(glColorTable);
    MAP_GL(glCullFace);
    MAP_GL(glDeleteTextures);
    MAP_GL(glDepthFunc);
    MAP_GL(glDepthMask);
    MAP_GL(glDepthRange);
    MAP_GL(glDisable);
    MAP_GL(glDisableClientState);
    MAP_GL(glDrawArrays);
    MAP_GL(glDrawBuffer);
    MAP_GL(glDrawElements);
    MAP_GL(glEnable);
    MAP_GL(glEnableClientState);
    MAP_GL(glEnd);
    MAP_GL(glFinish);
    MAP_GL(glFlush);
    MAP_GL(glFogf);
    MAP_GL(glFogfv);
    MAP_GL(glFogi);
    MAP_GL(glFrontFace);
    MAP_GL(glFrustum);
    MAP_GL(glGenTextures);
    MAP_GL(glGetBooleanv);
    MAP_GL(glGetError);
    MAP_GL(glGetFloatv);
    MAP_GL(glGetIntegerv);
    MAP_GL(glGetString);
    MAP_GL(glHint);
    MAP_GL(glIsEnabled);
    MAP_GL(glLoadIdentity);
    MAP_GL(glLoadMatrixd);
    MAP_GL(glLoadMatrixf);
    MAP_GL(glMatrixMode);
    MAP_GL(glMultMatrixd);
    MAP_GL(glMultMatrixf);
    MAP_GL(glNormal3f);
    MAP_GL(glOrtho);
    MAP_GL(glPixelStorei);
    MAP_GL(glPointSize);
    MAP_GL(glPolygonMode);
    MAP_GL(glPopMatrix);
    MAP_GL(glPushMatrix);
    MAP_GL(glReadPixels);
    MAP_GL(glRotated);
    MAP_GL(glRotatef);
    MAP_GL(glScaled);
    MAP_GL(glScalef);
    MAP_GL(glScissor);
    MAP_GL(glShadeModel);
    MAP_GL(glTexCoord2f);
    MAP_GL(glTexCoord2fv);
    MAP_GL(glTexCoord4f);
    MAP_GL(glTexCoord4fv);
    MAP_GL(glTexCoordPointer);
    MAP_GL(glTexEnvf);
    MAP_GL(glTexEnvfv);
    MAP_GL(glTexEnvi);
    MAP_GL(glTexEnviv);
    MAP_GL(glTexGeni);
    MAP_GL(glTexImage2D);
    MAP_GL(glTexParameterf);
    MAP_GL(glTexParameteri);
    MAP_GL(glTexSubImage2D);
    MAP_GL(glTranslated);
    MAP_GL(glTranslatef);
    MAP_GL(glVertex2f);
    MAP_GL(glVertex2fv);
    MAP_GL(glVertex3f);
    MAP_GL(glVertex3fv);
    MAP_GL(glVertex4f);
    MAP_GL(glVertex4fv);
    MAP_GL(glVertexPointer);
    MAP_GL(glViewport);
    MAP_GL(gluLookAt);
    MAP_GL(gluPerspective);

    MAP_COMPAT(glInterleavedArrays);
    MAP_COMPAT(glPixelStoref);
    MAP_COMPAT(glTexParameterfv);
    MAP_COMPAT(glTexParameteriv);
    MAP_COMPAT(glVertex2i);

#undef MAP_COMPAT
#undef MAP_GL
    return NULL;
}
