#include "test_api.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
void UETraceStart(int,int,int);
void UETraceStop(void);
int ReplayMain(int,char**);
static GLcontext ctx;
static unsigned calls[38], nextid=10, latestid, imagehash, floats_seen;
static int quit=0;
static struct Window window={(void*)1};
static unsigned hash(const unsigned char *p,unsigned n) { unsigned h=1; while(n--) h=h*33+*p++; return h; }
static void stub_GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref) { ++calls[1]; }
static void stub_GLBegin(GLcontext context, GLenum mode) { ++calls[2]; }
static void stub_GLBindTexture(GLcontext context, GLenum target, GLuint texture) { ++calls[3]; assert(texture==latestid); }
static void stub_GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor) { ++calls[4]; }
static void stub_GLClear(GLcontext context, GLbitfield mask) { ++calls[5]; }
static void stub_GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) { ++calls[6]; }
static void stub_GLClearDepth(GLcontext context, GLclampd depth) { ++calls[7]; }
static void stub_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha) { ++calls[8]; }
static void stub_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) { ++calls[9]; }
static void stub_GLDepthFunc(GLcontext context, GLenum func) { ++calls[10]; }
static void stub_GLDepthMask(GLcontext context, GLboolean flag) { ++calls[11]; }
static void stub_GLEnd(GLcontext context) { ++calls[12]; }
static void stub_GLFinish(GLcontext context) { ++calls[13]; }
static void stub_GLFlush(GLcontext context) { ++calls[14]; }
static void stub_GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar) { ++calls[15]; assert(left==-1.333333 && right==1.333333 && bottom==-1 && top==1 && zNear==1 && zFar==100); }
static void stub_GLLoadIdentity(GLcontext context) { ++calls[16]; }
static void stub_GLMatrixMode(GLcontext context, GLenum mode) { ++calls[17]; }
static void stub_GLPixelStorei(GLcontext context, GLenum pname, GLint param) { ++calls[18]; }
static void stub_GLShadeModel(GLcontext context, GLenum mode) { ++calls[19]; }
static void stub_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t) { ++calls[20]; }
static void stub_GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param) { ++calls[21]; }
static void stub_GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param) { ++calls[22]; }
static void stub_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w) { ++calls[23]; assert(x==1.25f && y==-2.5f && z==9.75f && w==1.0f); ++floats_seen; }
static void stub_GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height) { ++calls[24]; }
static void stub_MGLSetState(GLcontext context, GLenum cap, GLboolean state) { ++calls[25]; }
static void stub_MGLLockMode(GLcontext context, GLenum lockMode) { ++calls[26]; }
static void stub_MGLEnableSync(GLcontext context, GLboolean enable) { ++calls[27]; }
static void stub_GLActiveTextureARB(GLcontext context, GLenum unit) { ++calls[28]; }
static void stub_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t) { ++calls[29]; }
static void stub_GLColor4fv(GLcontext context, GLfloat *v) { ++calls[30]; assert(v[0]==0.25f && v[3]==1.0f); }
static void stub_GLVertex3fv(GLcontext context, GLfloat *v) { ++calls[31]; }
static void stub_GLMultMatrixf(GLcontext context, const GLfloat *m) { ++calls[32]; }
static void stub_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures) { ++calls[33]; for(int i=0;i<n;++i) textures[i]=latestid=nextid++; }
static void stub_GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures) { ++calls[34]; }
static void stub_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels) { ++calls[35]; assert(width==1 && height==1 && format==GL_RGB); imagehash=hash(pixels,3); }
static void stub_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels) { ++calls[36]; }
static void stub_MGLSwitchDisplay(GLcontext context) { ++calls[37]; }
static void palette(GLcontext c,GLenum a,GLenum b,GLint d,GLenum e,GLenum f,GLvoid*g) {}
static MGLDispatchTable table={
 .abiVersion=3,.structSize=sizeof(MGLDispatchTable),.backendFlags=4,.currentContext=&ctx,
 .GLAlphaFunc=stub_GLAlphaFunc,
 .GLBegin=stub_GLBegin,
 .GLBindTexture=stub_GLBindTexture,
 .GLBlendFunc=stub_GLBlendFunc,
 .GLClear=stub_GLClear,
 .GLClearColor=stub_GLClearColor,
 .GLClearDepth=stub_GLClearDepth,
 .GLColor4f=stub_GLColor4f,
 .GLColorMask=stub_GLColorMask,
 .GLDepthFunc=stub_GLDepthFunc,
 .GLDepthMask=stub_GLDepthMask,
 .GLEnd=stub_GLEnd,
 .GLFinish=stub_GLFinish,
 .GLFlush=stub_GLFlush,
 .GLFrustum=stub_GLFrustum,
 .GLLoadIdentity=stub_GLLoadIdentity,
 .GLMatrixMode=stub_GLMatrixMode,
 .GLPixelStorei=stub_GLPixelStorei,
 .GLShadeModel=stub_GLShadeModel,
 .GLTexCoord2f=stub_GLTexCoord2f,
 .GLTexEnvi=stub_GLTexEnvi,
 .GLTexParameteri=stub_GLTexParameteri,
 .GLVertex4f=stub_GLVertex4f,
 .GLViewport=stub_GLViewport,
 .MGLSetState=stub_MGLSetState,
 .MGLLockMode=stub_MGLLockMode,
 .MGLEnableSync=stub_MGLEnableSync,
 .GLActiveTextureARB=stub_GLActiveTextureARB,
 .GLMultiTexCoord2fARB=stub_GLMultiTexCoord2fARB,
 .GLColor4fv=stub_GLColor4fv,
 .GLVertex3fv=stub_GLVertex3fv,
 .GLMultMatrixf=stub_GLMultMatrixf,
 .GLGenTextures=stub_GLGenTextures,
 .GLDeleteTextures=stub_GLDeleteTextures,
 .GLTexImage2D=stub_GLTexImage2D,
 .GLTexSubImage2D=stub_GLTexSubImage2D,
 .MGLSwitchDisplay=stub_MGLSwitchDisplay,
 .GLColorTable=palette
};
const MGLDispatchTable *MiniGLDispatch=&table;
int MiniGLOpen(void) { return 1; }
void MiniGLClose(void) {}
void mglChooseWindowMode(int n) {}
void mglChooseNumberOfBuffers(int n) {}
void mglChoosePixelDepth(int n) {}
void mglChooseVertexBufferSize(int n) {}
int mglCreateContext(int x,int y,int w,int h) { return 1; }
void *mglGetInputWindowHandle(void) { return &window; }
void *mglGetWindowHandle(void) { return &window; }
void mglDeleteContext(void) {}
void mglUnlockDisplay(void) {}
int ModifyIDCMP(struct Window*w,unsigned flags) { return 1; }
void SetWindowTitles(struct Window*w,char*t,STRPTR s) {}
void *GetMsg(void*p) { static struct IntuiMessage m={IDCMP_CLOSEWINDOW,0}; if(quit) {quit=0;return &m;} return NULL; }
void ReplyMsg(struct Message*m) {}
void WaitPort(void*p) { quit=1; }
int main(void) {
 GLuint id; unsigned char pixels[3]={12,34,56};
 GLfloat color[4]={0.25f,0.5f,0.75f,1.0f};
 unsigned expected[38], expected_hash=hash(pixels,3);
 char *args[]={"MGL-replay","PROGDIR:ue1-mgl.cap",NULL};
 UETraceStart(640,480,0);
 assert(MiniGLDispatch!=&table);
 MiniGLDispatch->GLGenTextures(ctx,1,&id);
 MiniGLDispatch->GLBindTexture(ctx,0x0de1,id);
 MiniGLDispatch->GLPixelStorei(ctx,GL_UNPACK_ALIGNMENT,1);
 MiniGLDispatch->GLTexImage2D(ctx,0x0de1,0,GL_RGB,1,1,0,GL_RGB,GL_UNSIGNED_BYTE,pixels);
 memset(pixels,0,sizeof(pixels)); /* Recorded pixels must own a copy. */
 MiniGLDispatch->GLFrustum(ctx,-1.333333,1.333333,-1,1,1,100);
 MiniGLDispatch->GLColor4fv(ctx,color);
 memset(color,0,sizeof(color));
 MiniGLDispatch->GLBegin(ctx,6);
 MiniGLDispatch->GLVertex4f(ctx,1.25f,-2.5f,9.75f,1.0f);
 MiniGLDispatch->GLEnd(ctx);
 MiniGLDispatch->MGLSwitchDisplay(ctx);
 UETraceStop();
 assert(MiniGLDispatch==&table);
 memcpy(expected,calls,sizeof(calls)); memset(calls,0,sizeof(calls));
 nextid=100; imagehash=0; floats_seen=0;
 assert(ReplayMain(2,args)==0);
 assert(!memcmp(expected,calls,sizeof(calls)));
 assert(imagehash==expected_hash && floats_seen==1 && latestid==100);
 /* Corrupt/truncated input must fail, not be treated as a successful replay. */
 FILE*f=fopen("bad.cap","wb"); assert(f); fputs("bad",f); fclose(f);
 args[1]="bad.cap"; assert(ReplayMain(2,args)!=0);
 puts("PASS: capture/replay calls, texture ID remap, owned pixel/vector payloads, RGB padding, scalars, truncation");
 return 0;
}

