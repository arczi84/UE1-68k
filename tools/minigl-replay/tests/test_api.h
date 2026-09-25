#ifndef TRACE_TEST_API_H
#define TRACE_TEST_API_H
#include <stdint.h>
typedef uint32_t GLenum, GLuint, GLbitfield;
typedef int32_t GLint, GLsizei;
typedef uint8_t GLboolean;
typedef float GLfloat, GLclampf;
typedef double GLdouble, GLclampd;
typedef void GLvoid;
typedef void *GLcontext;
typedef char *STRPTR;
#define GL_RGBA 0x1908
#define GL_RGB 0x1907
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNPACK_ALIGNMENT 0xcf5
#define GL_TRUE 1
#define GL_FALSE 0
#define MINIGL_DISPATCH_ABI_VERSION 3
#define IDCMP_RAWKEY 1
#define IDCMP_CLOSEWINDOW 2
typedef struct {
 uint32_t abiVersion,structSize,backendFlags; GLcontext *currentContext;
 void (*GLAlphaFunc)(GLcontext context, GLenum func, GLclampf ref);
 void (*GLBegin)(GLcontext context, GLenum mode);
 void (*GLBindTexture)(GLcontext context, GLenum target, GLuint texture);
 void (*GLBlendFunc)(GLcontext context, GLenum sfactor, GLenum dfactor);
 void (*GLClear)(GLcontext context, GLbitfield mask);
 void (*GLClearColor)(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha);
 void (*GLClearDepth)(GLcontext context, GLclampd depth);
 void (*GLColor4f)(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
 void (*GLColorMask)(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha);
 void (*GLDepthFunc)(GLcontext context, GLenum func);
 void (*GLDepthMask)(GLcontext context, GLboolean flag);
 void (*GLEnd)(GLcontext context);
 void (*GLFinish)(GLcontext context);
 void (*GLFlush)(GLcontext context);
 void (*GLFrustum)(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar);
 void (*GLLoadIdentity)(GLcontext context);
 void (*GLMatrixMode)(GLcontext context, GLenum mode);
 void (*GLPixelStorei)(GLcontext context, GLenum pname, GLint param);
 void (*GLShadeModel)(GLcontext context, GLenum mode);
 void (*GLTexCoord2f)(GLcontext context, GLfloat s, GLfloat t);
 void (*GLTexEnvi)(GLcontext context, GLenum target, GLenum pname, GLint param);
 void (*GLTexParameteri)(GLcontext context, GLenum target, GLenum pname, GLint param);
 void (*GLVertex4f)(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
 void (*GLViewport)(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height);
 void (*MGLSetState)(GLcontext context, GLenum cap, GLboolean state);
 void (*MGLLockMode)(GLcontext context, GLenum lockMode);
 void (*MGLEnableSync)(GLcontext context, GLboolean enable);
 void (*GLActiveTextureARB)(GLcontext context, GLenum unit);
 void (*GLMultiTexCoord2fARB)(GLcontext context, GLenum unit, GLfloat s, GLfloat t);
 void (*GLColor4fv)(GLcontext context, GLfloat *v);
 void (*GLVertex3fv)(GLcontext context, GLfloat *v);
 void (*GLMultMatrixf)(GLcontext context, const GLfloat *m);
 void (*GLGenTextures)(GLcontext context, GLsizei n, GLuint *textures);
 void (*GLDeleteTextures)(GLcontext context, GLsizei n, const GLuint *textures);
 void (*GLTexImage2D)(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels);
 void (*GLTexSubImage2D)(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels);
 void (*MGLSwitchDisplay)(GLcontext context);
 void (*GLColorTable)(GLcontext,GLenum,GLenum,GLint,GLenum,GLenum,GLvoid*);
 void (*GLReadPixels)(GLcontext,GLint,GLint,GLsizei,GLsizei,GLenum,GLenum,GLvoid*);
} MGLDispatchTable;
extern const MGLDispatchTable *MiniGLDispatch;
#define MGLD_CTX (*MiniGLDispatch->currentContext)
#define glGenTextures(n,t) MiniGLDispatch->GLGenTextures(MGLD_CTX,n,t)
#define glDeleteTextures(n,t) MiniGLDispatch->GLDeleteTextures(MGLD_CTX,n,t)
struct Message { int unused; };
struct IntuiMessage { unsigned Class,Code; };
struct Window { void *UserPort; };
int MiniGLOpen(void);
void MiniGLClose(void);
void mglChooseWindowMode(int);
void mglChooseNumberOfBuffers(int);
void mglChoosePixelDepth(int);
void mglChooseVertexBufferSize(int);
int mglCreateContext(int,int,int,int);
void *mglGetInputWindowHandle(void);
void *mglGetWindowHandle(void);
void mglDeleteContext(void);
void mglUnlockDisplay(void);
int ModifyIDCMP(struct Window*,unsigned);
void SetWindowTitles(struct Window*,char*,STRPTR);
void *GetMsg(void*);
void ReplyMsg(struct Message*);
void WaitPort(void*);
#endif
