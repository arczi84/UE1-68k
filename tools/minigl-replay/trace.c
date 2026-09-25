/* UE MiniGL dispatch capture/replay. Native big-endian Amiga format v1.
 * Records actual arguments and copies pointed-to texture/vertex data.
 * No graphics-library changes; only this process's dispatch pointer is replaced. */
#ifdef TRACE_HOST_TEST
#include "tests/test_api.h"
#else
#include <clib/minigl_open_protos.h>
#include <proto/minigl.h>
#include <proto/intuition.h>
#include <proto/exec.h>
#include <intuition/intuition.h>
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define TRACE_MAGIC 0x55454731UL
#define TRACE_LIMIT (128UL*1024UL*1024UL)
#define BUFFER_SIZE (8UL*1024UL*1024UL)
#define FRAME_LIMIT 64
typedef struct { uint32_t op, bytes, a[12]; } Packet;
typedef struct { uint32_t magic, version, width, height, backend, frames, windowed; } Header;
static int unpack_alignment = 4;
static unsigned image_bytes(int w, int h, unsigned format, unsigned type)
{
    unsigned row, stride, channels;
    if(w<0 || h<0 || w>4096 || h>4096 || type!=GL_UNSIGNED_BYTE) return 0xffffffffUL;
    channels = format==GL_RGBA ? 4 : format==GL_RGB ? 3 : 0;
    if(!channels) return 0xffffffffUL;
    row=(unsigned)w*channels;
    stride=(row+unpack_alignment-1)&~(unpack_alignment-1);
    return h ? stride*((unsigned)h-1)+row : 0;
}
#ifdef MGL_CAPTURE
static MGLDispatchTable hook_table = {1};
static const MGLDispatchTable *original = NULL;
static FILE *capture_file = NULL;
static unsigned char *buffer = NULL;
static unsigned used = 0, total = 0, frame_count = 0;
static int failed = 0;
static int started = -1;
static Header header = {TRACE_MAGIC,1,0,0,0,0,1};
static void capture_error(void) { failed=1; }
static void save_packet(Packet *p, const void *payload, unsigned bytes)
{
    unsigned padded;
    if(!capture_file || failed) return;
    if(bytes==0xffffffffUL || bytes>BUFFER_SIZE-sizeof(*p)-3) {
        capture_error(); return;
    }
    padded=(bytes+3)&~3U;
    if(used>BUFFER_SIZE-sizeof(*p)-padded || total+used+sizeof(*p)+padded>TRACE_LIMIT) {
        capture_error(); return;
    }
    p->bytes=bytes;
    memcpy(buffer+used,p,sizeof(*p)); used+=sizeof(*p);
    if(bytes) memcpy(buffer+used,payload,bytes);
    if(padded>bytes) memset(buffer+used+bytes,0,padded-bytes);
    used+=padded;
}
void UETraceStop(void)
{
    if(started != 1 || !capture_file) return;
    MiniGLDispatch=original;
    if(used && fwrite(buffer,1,used,capture_file)!=used) failed=1;
    header.frames=failed ? 0 : frame_count;
    if(fseek(capture_file,0,SEEK_SET) || fwrite(&header,1,sizeof(header),capture_file)!=sizeof(header)) failed=1;
    if(fclose(capture_file)) failed=1;
    capture_file=NULL; free(buffer); buffer=NULL; started=0;
    fprintf(stderr, failed ? "MGL capture FAILED/incomplete; do not replay.\n" :
            "MGL capture saved: PROGDIR:ue1-mgl.cap (%u frames)\n", frame_count);
}
static void frame_done(void)
{
    ++frame_count;
    if(!failed && fwrite(buffer,1,used,capture_file)!=used) failed=1;
    total+=used; used=0;
    if(failed || frame_count==FRAME_LIMIT || total>=TRACE_LIMIT-BUFFER_SIZE) UETraceStop();
}
static void cap_GLAlphaFunc(GLcontext context, GLenum func, GLclampf ref)
{
    Packet p={0}; p.op=1;
    memcpy(&p.a[0], &func, sizeof(func));
    memcpy(&p.a[1], &ref, sizeof(ref));
    save_packet(&p,NULL,0);
    original->GLAlphaFunc(context, func, ref);
}
static void cap_GLBegin(GLcontext context, GLenum mode)
{
    Packet p={0}; p.op=2;
    memcpy(&p.a[0], &mode, sizeof(mode));
    save_packet(&p,NULL,0);
    original->GLBegin(context, mode);
}
static void cap_GLBindTexture(GLcontext context, GLenum target, GLuint texture)
{
    Packet p={0}; p.op=3;
    memcpy(&p.a[0], &target, sizeof(target));
    memcpy(&p.a[1], &texture, sizeof(texture));
    save_packet(&p,NULL,0);
    original->GLBindTexture(context, target, texture);
}
static void cap_GLBlendFunc(GLcontext context, GLenum sfactor, GLenum dfactor)
{
    Packet p={0}; p.op=4;
    memcpy(&p.a[0], &sfactor, sizeof(sfactor));
    memcpy(&p.a[1], &dfactor, sizeof(dfactor));
    save_packet(&p,NULL,0);
    original->GLBlendFunc(context, sfactor, dfactor);
}
static void cap_GLClear(GLcontext context, GLbitfield mask)
{
    Packet p={0}; p.op=5;
    memcpy(&p.a[0], &mask, sizeof(mask));
    save_packet(&p,NULL,0);
    original->GLClear(context, mask);
}
static void cap_GLClearColor(GLcontext context, GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha)
{
    Packet p={0}; p.op=6;
    memcpy(&p.a[0], &red, sizeof(red));
    memcpy(&p.a[1], &green, sizeof(green));
    memcpy(&p.a[2], &blue, sizeof(blue));
    memcpy(&p.a[3], &alpha, sizeof(alpha));
    save_packet(&p,NULL,0);
    original->GLClearColor(context, red, green, blue, alpha);
}
static void cap_GLClearDepth(GLcontext context, GLclampd depth)
{
    Packet p={0}; p.op=7;
    memcpy(&p.a[0], &depth, sizeof(depth));
    save_packet(&p,NULL,0);
    original->GLClearDepth(context, depth);
}
static void cap_GLColor4f(GLcontext context, GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    Packet p={0}; p.op=8;
    memcpy(&p.a[0], &red, sizeof(red));
    memcpy(&p.a[1], &green, sizeof(green));
    memcpy(&p.a[2], &blue, sizeof(blue));
    memcpy(&p.a[3], &alpha, sizeof(alpha));
    save_packet(&p,NULL,0);
    original->GLColor4f(context, red, green, blue, alpha);
}
static void cap_GLColorMask(GLcontext context, GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha)
{
    Packet p={0}; p.op=9;
    memcpy(&p.a[0], &red, sizeof(red));
    memcpy(&p.a[1], &green, sizeof(green));
    memcpy(&p.a[2], &blue, sizeof(blue));
    memcpy(&p.a[3], &alpha, sizeof(alpha));
    save_packet(&p,NULL,0);
    original->GLColorMask(context, red, green, blue, alpha);
}
static void cap_GLDepthFunc(GLcontext context, GLenum func)
{
    Packet p={0}; p.op=10;
    memcpy(&p.a[0], &func, sizeof(func));
    save_packet(&p,NULL,0);
    original->GLDepthFunc(context, func);
}
static void cap_GLDepthMask(GLcontext context, GLboolean flag)
{
    Packet p={0}; p.op=11;
    memcpy(&p.a[0], &flag, sizeof(flag));
    save_packet(&p,NULL,0);
    original->GLDepthMask(context, flag);
}
static void cap_GLEnd(GLcontext context)
{
    Packet p={0}; p.op=12;
    save_packet(&p,NULL,0);
    original->GLEnd(context);
}
static void cap_GLFinish(GLcontext context)
{
    Packet p={0}; p.op=13;
    save_packet(&p,NULL,0);
    original->GLFinish(context);
}
static void cap_GLFlush(GLcontext context)
{
    Packet p={0}; p.op=14;
    save_packet(&p,NULL,0);
    original->GLFlush(context);
}
static void cap_GLFrustum(GLcontext context, GLdouble left, GLdouble right, GLdouble bottom, GLdouble top, GLdouble zNear, GLdouble zFar)
{
    Packet p={0}; p.op=15;
    memcpy(&p.a[0], &left, sizeof(left));
    memcpy(&p.a[2], &right, sizeof(right));
    memcpy(&p.a[4], &bottom, sizeof(bottom));
    memcpy(&p.a[6], &top, sizeof(top));
    memcpy(&p.a[8], &zNear, sizeof(zNear));
    memcpy(&p.a[10], &zFar, sizeof(zFar));
    save_packet(&p,NULL,0);
    original->GLFrustum(context, left, right, bottom, top, zNear, zFar);
}
static void cap_GLLoadIdentity(GLcontext context)
{
    Packet p={0}; p.op=16;
    save_packet(&p,NULL,0);
    original->GLLoadIdentity(context);
}
static void cap_GLMatrixMode(GLcontext context, GLenum mode)
{
    Packet p={0}; p.op=17;
    memcpy(&p.a[0], &mode, sizeof(mode));
    save_packet(&p,NULL,0);
    original->GLMatrixMode(context, mode);
}
static void cap_GLPixelStorei(GLcontext context, GLenum pname, GLint param)
{
    Packet p={0}; p.op=18;
    memcpy(&p.a[0], &pname, sizeof(pname));
    memcpy(&p.a[1], &param, sizeof(param));
    save_packet(&p,NULL,0);
    if(pname==GL_UNPACK_ALIGNMENT) { if(param!=1 && param!=2 && param!=4 && param!=8) capture_error(); else unpack_alignment=param; }
    original->GLPixelStorei(context, pname, param);
}
static void cap_GLShadeModel(GLcontext context, GLenum mode)
{
    Packet p={0}; p.op=19;
    memcpy(&p.a[0], &mode, sizeof(mode));
    save_packet(&p,NULL,0);
    original->GLShadeModel(context, mode);
}
static void cap_GLTexCoord2f(GLcontext context, GLfloat s, GLfloat t)
{
    Packet p={0}; p.op=20;
    memcpy(&p.a[0], &s, sizeof(s));
    memcpy(&p.a[1], &t, sizeof(t));
    save_packet(&p,NULL,0);
    original->GLTexCoord2f(context, s, t);
}
static void cap_GLTexEnvi(GLcontext context, GLenum target, GLenum pname, GLint param)
{
    Packet p={0}; p.op=21;
    memcpy(&p.a[0], &target, sizeof(target));
    memcpy(&p.a[1], &pname, sizeof(pname));
    memcpy(&p.a[2], &param, sizeof(param));
    save_packet(&p,NULL,0);
    original->GLTexEnvi(context, target, pname, param);
}
static void cap_GLTexParameteri(GLcontext context, GLenum target, GLenum pname, GLint param)
{
    Packet p={0}; p.op=22;
    memcpy(&p.a[0], &target, sizeof(target));
    memcpy(&p.a[1], &pname, sizeof(pname));
    memcpy(&p.a[2], &param, sizeof(param));
    save_packet(&p,NULL,0);
    original->GLTexParameteri(context, target, pname, param);
}
static void cap_GLVertex4f(GLcontext context, GLfloat x, GLfloat y, GLfloat z, GLfloat w)
{
    Packet p={0}; p.op=23;
    memcpy(&p.a[0], &x, sizeof(x));
    memcpy(&p.a[1], &y, sizeof(y));
    memcpy(&p.a[2], &z, sizeof(z));
    memcpy(&p.a[3], &w, sizeof(w));
    save_packet(&p,NULL,0);
    original->GLVertex4f(context, x, y, z, w);
}
static void cap_GLViewport(GLcontext context, GLint x, GLint y, GLsizei width, GLsizei height)
{
    Packet p={0}; p.op=24;
    memcpy(&p.a[0], &x, sizeof(x));
    memcpy(&p.a[1], &y, sizeof(y));
    memcpy(&p.a[2], &width, sizeof(width));
    memcpy(&p.a[3], &height, sizeof(height));
    save_packet(&p,NULL,0);
    original->GLViewport(context, x, y, width, height);
}
static void cap_MGLSetState(GLcontext context, GLenum cap, GLboolean state)
{
    Packet p={0}; p.op=25;
    memcpy(&p.a[0], &cap, sizeof(cap));
    memcpy(&p.a[1], &state, sizeof(state));
    save_packet(&p,NULL,0);
    original->MGLSetState(context, cap, state);
}
static void cap_MGLLockMode(GLcontext context, GLenum lockMode)
{
    Packet p={0}; p.op=26;
    memcpy(&p.a[0], &lockMode, sizeof(lockMode));
    save_packet(&p,NULL,0);
    original->MGLLockMode(context, lockMode);
}
static void cap_MGLEnableSync(GLcontext context, GLboolean enable)
{
    Packet p={0}; p.op=27;
    memcpy(&p.a[0], &enable, sizeof(enable));
    save_packet(&p,NULL,0);
    original->MGLEnableSync(context, enable);
}
static void cap_GLActiveTextureARB(GLcontext context, GLenum unit)
{
    Packet p={0}; p.op=28;
    memcpy(&p.a[0], &unit, sizeof(unit));
    save_packet(&p,NULL,0);
    original->GLActiveTextureARB(context, unit);
}
static void cap_GLMultiTexCoord2fARB(GLcontext context, GLenum unit, GLfloat s, GLfloat t)
{
    Packet p={0}; p.op=29;
    memcpy(&p.a[0], &unit, sizeof(unit));
    memcpy(&p.a[1], &s, sizeof(s));
    memcpy(&p.a[2], &t, sizeof(t));
    save_packet(&p,NULL,0);
    original->GLMultiTexCoord2fARB(context, unit, s, t);
}
static void cap_GLColor4fv(GLcontext context, GLfloat *v)
{
    Packet p={0}; p.op=30;
    save_packet(&p,v,16);
    original->GLColor4fv(context, v);
}
static void cap_GLVertex3fv(GLcontext context, GLfloat *v)
{
    Packet p={0}; p.op=31;
    save_packet(&p,v,12);
    original->GLVertex3fv(context, v);
}
static void cap_GLMultMatrixf(GLcontext context, const GLfloat *m)
{
    Packet p={0}; p.op=32;
    save_packet(&p,m,64);
    original->GLMultMatrixf(context, m);
}
static void cap_GLGenTextures(GLcontext context, GLsizei n, GLuint *textures)
{
    Packet p={0}; p.op=33;
    original->GLGenTextures(context, n, textures);
    if(n<0 || n>65536) capture_error();
    memcpy(&p.a[0], &n, sizeof(n));
    save_packet(&p,textures,n*4);
}
static void cap_GLDeleteTextures(GLcontext context, GLsizei n, const GLuint *textures)
{
    Packet p={0}; p.op=34;
    if(n<0 || n>65536) capture_error();
    memcpy(&p.a[0], &n, sizeof(n));
    save_packet(&p,textures,n*4);
    original->GLDeleteTextures(context, n, textures);
}
static void cap_GLTexImage2D(GLcontext context, GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const GLvoid *pixels)
{
    Packet p={0}; p.op=35;
    memcpy(&p.a[0], &target, sizeof(target));
    memcpy(&p.a[1], &level, sizeof(level));
    memcpy(&p.a[2], &internalformat, sizeof(internalformat));
    memcpy(&p.a[3], &width, sizeof(width));
    memcpy(&p.a[4], &height, sizeof(height));
    memcpy(&p.a[5], &border, sizeof(border));
    memcpy(&p.a[6], &format, sizeof(format));
    memcpy(&p.a[7], &type, sizeof(type));
    p.a[11] = pixels != NULL;
    save_packet(&p,pixels,pixels ? image_bytes(width,height,format,type) : 0);
    original->GLTexImage2D(context, target, level, internalformat, width, height, border, format, type, pixels);
}
static void cap_GLTexSubImage2D(GLcontext context, GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height, GLenum format, GLenum type, const GLvoid *pixels)
{
    Packet p={0}; p.op=36;
    memcpy(&p.a[0], &target, sizeof(target));
    memcpy(&p.a[1], &level, sizeof(level));
    memcpy(&p.a[2], &xoffset, sizeof(xoffset));
    memcpy(&p.a[3], &yoffset, sizeof(yoffset));
    memcpy(&p.a[4], &width, sizeof(width));
    memcpy(&p.a[5], &height, sizeof(height));
    memcpy(&p.a[6], &format, sizeof(format));
    memcpy(&p.a[7], &type, sizeof(type));
    p.a[11] = pixels != NULL;
    save_packet(&p,pixels,pixels ? image_bytes(width,height,format,type) : 0);
    original->GLTexSubImage2D(context, target, level, xoffset, yoffset, width, height, format, type, pixels);
}
static void cap_MGLSwitchDisplay(GLcontext context)
{
    Packet p={0}; p.op=37;
    save_packet(&p,NULL,0);
    original->MGLSwitchDisplay(context);
    frame_done();
}

static void unsupported_palette(GLcontext context, GLenum target, GLenum internalformat,
 GLint width, GLenum format, GLenum type, GLvoid *data)
{
    capture_error();
    original->GLColorTable(context,target,internalformat,width,format,type,data);
}
static void unsupported_readback(GLcontext context, GLint x, GLint y, GLsizei width,
 GLsizei height, GLenum format, GLenum type, GLvoid *pixels)
{
    /* Do not silently omit the synchronization introduced by a screenshot. */
    capture_error();
    original->GLReadPixels(context,x,y,width,height,format,type,pixels);
}
void UETraceStart(int width, int height, int fullscreen)
{
    FILE *existing;
    if(started != -1) return;
    capture_file=NULL; buffer=NULL;
    existing=fopen("PROGDIR:ue1-mgl.cap","rb");
    if(existing) { fclose(existing); fputs("MGL capture: ue1-mgl.cap exists; rename it first.\n",stderr); return; }
    if(MiniGLDispatch->abiVersion!=MINIGL_DISPATCH_ABI_VERSION ||
       MiniGLDispatch->structSize<sizeof(MGLDispatchTable)) {
        fputs("MGL capture: incompatible dispatch table.\n",stderr); return;
    }
    buffer=malloc(BUFFER_SIZE);
    if(!buffer) { fputs("MGL capture: cannot allocate 8 MB.\n",stderr); return; }
    capture_file=fopen("PROGDIR:ue1-mgl.cap","wb");
    if(!capture_file) { free(buffer); buffer=NULL; fputs("MGL capture: cannot create file.\n",stderr); return; }
    started=1;
    original=MiniGLDispatch;
    hook_table=*original;
    hook_table.GLAlphaFunc=cap_GLAlphaFunc;
    hook_table.GLBegin=cap_GLBegin;
    hook_table.GLBindTexture=cap_GLBindTexture;
    hook_table.GLBlendFunc=cap_GLBlendFunc;
    hook_table.GLClear=cap_GLClear;
    hook_table.GLClearColor=cap_GLClearColor;
    hook_table.GLClearDepth=cap_GLClearDepth;
    hook_table.GLColor4f=cap_GLColor4f;
    hook_table.GLColorMask=cap_GLColorMask;
    hook_table.GLDepthFunc=cap_GLDepthFunc;
    hook_table.GLDepthMask=cap_GLDepthMask;
    hook_table.GLEnd=cap_GLEnd;
    hook_table.GLFinish=cap_GLFinish;
    hook_table.GLFlush=cap_GLFlush;
    hook_table.GLFrustum=cap_GLFrustum;
    hook_table.GLLoadIdentity=cap_GLLoadIdentity;
    hook_table.GLMatrixMode=cap_GLMatrixMode;
    hook_table.GLPixelStorei=cap_GLPixelStorei;
    hook_table.GLShadeModel=cap_GLShadeModel;
    hook_table.GLTexCoord2f=cap_GLTexCoord2f;
    hook_table.GLTexEnvi=cap_GLTexEnvi;
    hook_table.GLTexParameteri=cap_GLTexParameteri;
    hook_table.GLVertex4f=cap_GLVertex4f;
    hook_table.GLViewport=cap_GLViewport;
    hook_table.MGLSetState=cap_MGLSetState;
    hook_table.MGLLockMode=cap_MGLLockMode;
    hook_table.MGLEnableSync=cap_MGLEnableSync;
    hook_table.GLActiveTextureARB=cap_GLActiveTextureARB;
    hook_table.GLMultiTexCoord2fARB=cap_GLMultiTexCoord2fARB;
    hook_table.GLColor4fv=cap_GLColor4fv;
    hook_table.GLVertex3fv=cap_GLVertex3fv;
    hook_table.GLMultMatrixf=cap_GLMultMatrixf;
    hook_table.GLGenTextures=cap_GLGenTextures;
    hook_table.GLDeleteTextures=cap_GLDeleteTextures;
    hook_table.GLTexImage2D=cap_GLTexImage2D;
    hook_table.GLTexSubImage2D=cap_GLTexSubImage2D;
    hook_table.MGLSwitchDisplay=cap_MGLSwitchDisplay;

    hook_table.GLColorTable=unsupported_palette;
    hook_table.GLReadPixels=unsupported_readback;
    used=total=frame_count=0; failed=0; unpack_alignment=4;
    header.width=width; header.height=height; header.backend=original->backendFlags; header.frames=0;
    header.windowed=!fullscreen;
    if(fwrite(&header,1,sizeof(header),capture_file)!=sizeof(header)) { failed=1; UETraceStop(); return; }
    MiniGLDispatch=&hook_table;
    fputs("MGL capture: recording first 64 swaps to PROGDIR:ue1-mgl.cap\n",stderr);
}
#else
static GLuint *texture_map = NULL;
static char title[160] = "UE MiniGL replay";
static int replay_packet(Packet *p, unsigned char *data)
{
    switch(p->op) {
    case 1: { /* GLAlphaFunc */
        GLenum func; memcpy(&func, &p->a[0], sizeof(func));
        GLclampf ref; memcpy(&ref, &p->a[1], sizeof(ref));
        if(p->bytes) return 0;
        MiniGLDispatch->GLAlphaFunc(MGLD_CTX, func, ref);
        return 1;
    }
    case 2: { /* GLBegin */
        GLenum mode; memcpy(&mode, &p->a[0], sizeof(mode));
        if(p->bytes) return 0;
        MiniGLDispatch->GLBegin(MGLD_CTX, mode);
        return 1;
    }
    case 3: { /* GLBindTexture */
        GLenum target; memcpy(&target, &p->a[0], sizeof(target));
        GLuint texture; memcpy(&texture, &p->a[1], sizeof(texture));
        if(p->bytes) return 0;
        if(texture>=65536 || (texture && !texture_map[texture])) return 0;
        texture=texture_map[texture];
        MiniGLDispatch->GLBindTexture(MGLD_CTX, target, texture);
        return 1;
    }
    case 4: { /* GLBlendFunc */
        GLenum sfactor; memcpy(&sfactor, &p->a[0], sizeof(sfactor));
        GLenum dfactor; memcpy(&dfactor, &p->a[1], sizeof(dfactor));
        if(p->bytes) return 0;
        MiniGLDispatch->GLBlendFunc(MGLD_CTX, sfactor, dfactor);
        return 1;
    }
    case 5: { /* GLClear */
        GLbitfield mask; memcpy(&mask, &p->a[0], sizeof(mask));
        if(p->bytes) return 0;
        MiniGLDispatch->GLClear(MGLD_CTX, mask);
        return 1;
    }
    case 6: { /* GLClearColor */
        GLclampf red; memcpy(&red, &p->a[0], sizeof(red));
        GLclampf green; memcpy(&green, &p->a[1], sizeof(green));
        GLclampf blue; memcpy(&blue, &p->a[2], sizeof(blue));
        GLclampf alpha; memcpy(&alpha, &p->a[3], sizeof(alpha));
        if(p->bytes) return 0;
        MiniGLDispatch->GLClearColor(MGLD_CTX, red, green, blue, alpha);
        return 1;
    }
    case 7: { /* GLClearDepth */
        GLclampd depth; memcpy(&depth, &p->a[0], sizeof(depth));
        if(p->bytes) return 0;
        MiniGLDispatch->GLClearDepth(MGLD_CTX, depth);
        return 1;
    }
    case 8: { /* GLColor4f */
        GLfloat red; memcpy(&red, &p->a[0], sizeof(red));
        GLfloat green; memcpy(&green, &p->a[1], sizeof(green));
        GLfloat blue; memcpy(&blue, &p->a[2], sizeof(blue));
        GLfloat alpha; memcpy(&alpha, &p->a[3], sizeof(alpha));
        if(p->bytes) return 0;
        MiniGLDispatch->GLColor4f(MGLD_CTX, red, green, blue, alpha);
        return 1;
    }
    case 9: { /* GLColorMask */
        GLboolean red; memcpy(&red, &p->a[0], sizeof(red));
        GLboolean green; memcpy(&green, &p->a[1], sizeof(green));
        GLboolean blue; memcpy(&blue, &p->a[2], sizeof(blue));
        GLboolean alpha; memcpy(&alpha, &p->a[3], sizeof(alpha));
        if(p->bytes) return 0;
        MiniGLDispatch->GLColorMask(MGLD_CTX, red, green, blue, alpha);
        return 1;
    }
    case 10: { /* GLDepthFunc */
        GLenum func; memcpy(&func, &p->a[0], sizeof(func));
        if(p->bytes) return 0;
        MiniGLDispatch->GLDepthFunc(MGLD_CTX, func);
        return 1;
    }
    case 11: { /* GLDepthMask */
        GLboolean flag; memcpy(&flag, &p->a[0], sizeof(flag));
        if(p->bytes) return 0;
        MiniGLDispatch->GLDepthMask(MGLD_CTX, flag);
        return 1;
    }
    case 12: { /* GLEnd */
        if(p->bytes) return 0;
        MiniGLDispatch->GLEnd(MGLD_CTX);
        return 1;
    }
    case 13: { /* GLFinish */
        if(p->bytes) return 0;
        MiniGLDispatch->GLFinish(MGLD_CTX);
        return 1;
    }
    case 14: { /* GLFlush */
        if(p->bytes) return 0;
        MiniGLDispatch->GLFlush(MGLD_CTX);
        return 1;
    }
    case 15: { /* GLFrustum */
        GLdouble left; memcpy(&left, &p->a[0], sizeof(left));
        GLdouble right; memcpy(&right, &p->a[2], sizeof(right));
        GLdouble bottom; memcpy(&bottom, &p->a[4], sizeof(bottom));
        GLdouble top; memcpy(&top, &p->a[6], sizeof(top));
        GLdouble zNear; memcpy(&zNear, &p->a[8], sizeof(zNear));
        GLdouble zFar; memcpy(&zFar, &p->a[10], sizeof(zFar));
        if(p->bytes) return 0;
        MiniGLDispatch->GLFrustum(MGLD_CTX, left, right, bottom, top, zNear, zFar);
        return 1;
    }
    case 16: { /* GLLoadIdentity */
        if(p->bytes) return 0;
        MiniGLDispatch->GLLoadIdentity(MGLD_CTX);
        return 1;
    }
    case 17: { /* GLMatrixMode */
        GLenum mode; memcpy(&mode, &p->a[0], sizeof(mode));
        if(p->bytes) return 0;
        MiniGLDispatch->GLMatrixMode(MGLD_CTX, mode);
        return 1;
    }
    case 18: { /* GLPixelStorei */
        GLenum pname; memcpy(&pname, &p->a[0], sizeof(pname));
        GLint param; memcpy(&param, &p->a[1], sizeof(param));
        if(p->bytes) return 0;
        if(pname==GL_UNPACK_ALIGNMENT) { if(param!=1 && param!=2 && param!=4 && param!=8) return 0; unpack_alignment=param; }
        MiniGLDispatch->GLPixelStorei(MGLD_CTX, pname, param);
        return 1;
    }
    case 19: { /* GLShadeModel */
        GLenum mode; memcpy(&mode, &p->a[0], sizeof(mode));
        if(p->bytes) return 0;
        MiniGLDispatch->GLShadeModel(MGLD_CTX, mode);
        return 1;
    }
    case 20: { /* GLTexCoord2f */
        GLfloat s; memcpy(&s, &p->a[0], sizeof(s));
        GLfloat t; memcpy(&t, &p->a[1], sizeof(t));
        if(p->bytes) return 0;
        MiniGLDispatch->GLTexCoord2f(MGLD_CTX, s, t);
        return 1;
    }
    case 21: { /* GLTexEnvi */
        GLenum target; memcpy(&target, &p->a[0], sizeof(target));
        GLenum pname; memcpy(&pname, &p->a[1], sizeof(pname));
        GLint param; memcpy(&param, &p->a[2], sizeof(param));
        if(p->bytes) return 0;
        MiniGLDispatch->GLTexEnvi(MGLD_CTX, target, pname, param);
        return 1;
    }
    case 22: { /* GLTexParameteri */
        GLenum target; memcpy(&target, &p->a[0], sizeof(target));
        GLenum pname; memcpy(&pname, &p->a[1], sizeof(pname));
        GLint param; memcpy(&param, &p->a[2], sizeof(param));
        if(p->bytes) return 0;
        MiniGLDispatch->GLTexParameteri(MGLD_CTX, target, pname, param);
        return 1;
    }
    case 23: { /* GLVertex4f */
        GLfloat x; memcpy(&x, &p->a[0], sizeof(x));
        GLfloat y; memcpy(&y, &p->a[1], sizeof(y));
        GLfloat z; memcpy(&z, &p->a[2], sizeof(z));
        GLfloat w; memcpy(&w, &p->a[3], sizeof(w));
        if(p->bytes) return 0;
        MiniGLDispatch->GLVertex4f(MGLD_CTX, x, y, z, w);
        return 1;
    }
    case 24: { /* GLViewport */
        GLint x; memcpy(&x, &p->a[0], sizeof(x));
        GLint y; memcpy(&y, &p->a[1], sizeof(y));
        GLsizei width; memcpy(&width, &p->a[2], sizeof(width));
        GLsizei height; memcpy(&height, &p->a[3], sizeof(height));
        if(p->bytes) return 0;
        MiniGLDispatch->GLViewport(MGLD_CTX, x, y, width, height);
        return 1;
    }
    case 25: { /* MGLSetState */
        GLenum cap; memcpy(&cap, &p->a[0], sizeof(cap));
        GLboolean state; memcpy(&state, &p->a[1], sizeof(state));
        if(p->bytes) return 0;
        MiniGLDispatch->MGLSetState(MGLD_CTX, cap, state);
        return 1;
    }
    case 26: { /* MGLLockMode */
        GLenum lockMode; memcpy(&lockMode, &p->a[0], sizeof(lockMode));
        if(p->bytes) return 0;
        MiniGLDispatch->MGLLockMode(MGLD_CTX, lockMode);
        return 1;
    }
    case 27: { /* MGLEnableSync */
        GLboolean enable; memcpy(&enable, &p->a[0], sizeof(enable));
        if(p->bytes) return 0;
        MiniGLDispatch->MGLEnableSync(MGLD_CTX, enable);
        return 1;
    }
    case 28: { /* GLActiveTextureARB */
        GLenum unit; memcpy(&unit, &p->a[0], sizeof(unit));
        if(p->bytes) return 0;
        MiniGLDispatch->GLActiveTextureARB(MGLD_CTX, unit);
        return 1;
    }
    case 29: { /* GLMultiTexCoord2fARB */
        GLenum unit; memcpy(&unit, &p->a[0], sizeof(unit));
        GLfloat s; memcpy(&s, &p->a[1], sizeof(s));
        GLfloat t; memcpy(&t, &p->a[2], sizeof(t));
        if(p->bytes) return 0;
        MiniGLDispatch->GLMultiTexCoord2fARB(MGLD_CTX, unit, s, t);
        return 1;
    }
    case 30: { /* GLColor4fv */
        if(p->bytes != (unsigned)(16)) return 0;
        GLfloat * v=(GLfloat *)data;
        MiniGLDispatch->GLColor4fv(MGLD_CTX, v);
        return 1;
    }
    case 31: { /* GLVertex3fv */
        if(p->bytes != (unsigned)(12)) return 0;
        GLfloat * v=(GLfloat *)data;
        MiniGLDispatch->GLVertex3fv(MGLD_CTX, v);
        return 1;
    }
    case 32: { /* GLMultMatrixf */
        if(p->bytes != (unsigned)(64)) return 0;
        const GLfloat * m=(const GLfloat *)data;
        MiniGLDispatch->GLMultMatrixf(MGLD_CTX, m);
        return 1;
    }
    case 33: { /* GLGenTextures */
        GLsizei n; memcpy(&n, &p->a[0], sizeof(n));
        if(n<0 || n>65536 || p->bytes != (unsigned)(n*4)) return 0;
        GLuint *textures=(GLuint*)data;
        for(int k=0;k<n;++k) { GLuint id=textures[k], actual; if(!id || id>=65536 || texture_map[id]) return 0; glGenTextures(1,&actual); texture_map[id]=actual; }
        return 1;
    }
    case 34: { /* GLDeleteTextures */
        GLsizei n; memcpy(&n, &p->a[0], sizeof(n));
        if(n<0 || n>65536 || p->bytes != (unsigned)(n*4)) return 0;
        const GLuint *textures=(const GLuint*)data;
        for(int k=0;k<n;++k) { GLuint id=textures[k]; if(id>=65536) return 0; GLuint actual=texture_map[id]; glDeleteTextures(1,&actual); texture_map[id]=0; }
        return 1;
    }
    case 35: { /* GLTexImage2D */
        GLenum target; memcpy(&target, &p->a[0], sizeof(target));
        GLint level; memcpy(&level, &p->a[1], sizeof(level));
        GLint internalformat; memcpy(&internalformat, &p->a[2], sizeof(internalformat));
        GLsizei width; memcpy(&width, &p->a[3], sizeof(width));
        GLsizei height; memcpy(&height, &p->a[4], sizeof(height));
        GLint border; memcpy(&border, &p->a[5], sizeof(border));
        GLenum format; memcpy(&format, &p->a[6], sizeof(format));
        GLenum type; memcpy(&type, &p->a[7], sizeof(type));
        unsigned expected=p->a[11] ? image_bytes(width,height,format,type) : 0;
        if(expected==0xffffffffUL || p->bytes!=expected) return 0;
        const GLvoid *pixels=p->a[11] ? data : NULL;
        MiniGLDispatch->GLTexImage2D(MGLD_CTX, target, level, internalformat, width, height, border, format, type, pixels);
        return 1;
    }
    case 36: { /* GLTexSubImage2D */
        GLenum target; memcpy(&target, &p->a[0], sizeof(target));
        GLint level; memcpy(&level, &p->a[1], sizeof(level));
        GLint xoffset; memcpy(&xoffset, &p->a[2], sizeof(xoffset));
        GLint yoffset; memcpy(&yoffset, &p->a[3], sizeof(yoffset));
        GLsizei width; memcpy(&width, &p->a[4], sizeof(width));
        GLsizei height; memcpy(&height, &p->a[5], sizeof(height));
        GLenum format; memcpy(&format, &p->a[6], sizeof(format));
        GLenum type; memcpy(&type, &p->a[7], sizeof(type));
        unsigned expected=p->a[11] ? image_bytes(width,height,format,type) : 0;
        if(expected==0xffffffffUL || p->bytes!=expected) return 0;
        const GLvoid *pixels=p->a[11] ? data : NULL;
        MiniGLDispatch->GLTexSubImage2D(MGLD_CTX, target, level, xoffset, yoffset, width, height, format, type, pixels);
        return 1;
    }
    case 37: { /* MGLSwitchDisplay */
        if(p->bytes) return 0;
        MiniGLDispatch->MGLSwitchDisplay(MGLD_CTX);
        return 2;
    }

    default: return 0;
    }
}
static int events(struct Window *window)
{
    struct IntuiMessage *msg; int running=1;
    while((msg=(struct IntuiMessage*)GetMsg(window->UserPort))) {
        if(msg->Class==IDCMP_CLOSEWINDOW || (msg->Class==IDCMP_RAWKEY && msg->Code==0x45)) running=0;
        ReplyMsg((struct Message*)msg);
    }
    return running;
}
int main(int argc, char **argv)
{
    FILE *f=NULL;
    Header h;
    unsigned char *data=NULL;
    long length;
    unsigned offset=0, frame=0, calls=0;
    int opened=0, context=0, result=1, running=1;
    struct Window *window=NULL;
    texture_map=NULL;
    if(argc!=2) { puts("Usage: MGL-replay ue1-mgl.cap"); return 1; }
    f=fopen(argv[1],"rb");
    if(!f) { puts("Cannot open capture file"); goto cleanup; }
    if(fread(&h,1,sizeof(h),f)!=sizeof(h) || h.magic!=TRACE_MAGIC || h.version!=1 ||
       !h.frames || h.frames>FRAME_LIMIT || !h.width || !h.height || h.width>4096 || h.height>4096 || h.windowed>1) {
        puts("Invalid/incomplete capture (native Amiga format v1 required)"); goto cleanup;
    }
    if(fseek(f,0,SEEK_END)) goto cleanup;
    length=ftell(f)-(long)sizeof(h);
    if(length<=0 || (unsigned long)length>TRACE_LIMIT || fseek(f,sizeof(h),SEEK_SET)) goto cleanup;
    data=malloc(length);
    texture_map=calloc(65536,sizeof(GLuint));
    if(!data || !texture_map) { puts("Not enough RAM to preload capture"); goto cleanup; }
    if(fread(data,1,length,f)!=(unsigned long)length) goto cleanup;
    fclose(f); f=NULL;
    if(!MiniGLOpen()) { puts("Cannot open minigl.library"); goto cleanup; }
    opened=1;
    printf("Capture backend=%lu replay backend=%lu, frames=%lu\n",
           (unsigned long)h.backend,(unsigned long)MiniGLDispatch->backendFlags,(unsigned long)h.frames);
    mglChooseWindowMode(h.windowed ? GL_TRUE : GL_FALSE); mglChooseNumberOfBuffers(2);
    mglChoosePixelDepth(16); mglChooseVertexBufferSize(4096);
    if(!mglCreateContext(0,0,h.width,h.height)) { puts("Cannot create context"); goto cleanup; }
    context=1;
    window=(struct Window*)mglGetInputWindowHandle();
    if(!window) window=(struct Window*)mglGetWindowHandle();
    if(!window || !ModifyIDCMP(window,IDCMP_RAWKEY|IDCMP_CLOSEWINDOW) || !window->UserPort) goto cleanup;
    unpack_alignment=4;
    while(offset<(unsigned long)length && running) {
        Packet p; int status;
        if((unsigned long)length-offset<sizeof(p)) goto invalid;
        memcpy(&p,data+offset,sizeof(p)); offset+=sizeof(p);
        if(p.bytes>TRACE_LIMIT || ((p.bytes+3)&~3U)>(unsigned long)length-offset) goto invalid;
        status=replay_packet(&p,data+offset);
        if(!status) goto invalid;
        offset+=(p.bytes+3)&~3U; ++calls;
        if(status==2) {
            ++frame;
            snprintf(title,sizeof(title),"UE replay: frame=%u/%lu calls=%u swap returned",frame,(unsigned long)h.frames,calls);
            SetWindowTitles(window,title,(STRPTR)-1);
            running=events(window);
        }
    }
    if(running && frame!=h.frames) goto invalid;
    result=0;
    snprintf(title,sizeof(title),"UE replay complete: %u frames, %u calls - ESC exits",frame,calls);
    SetWindowTitles(window,title,(STRPTR)-1);
    while(running) { WaitPort(window->UserPort); running=events(window); }
    goto cleanup;
invalid:
    fprintf(stderr,"Invalid/unsupported record at byte %u, call %u\n",offset,calls);
cleanup:
    if(context) { mglUnlockDisplay(); if(window) ModifyIDCMP(window,0); mglDeleteContext(); }
    if(opened) MiniGLClose();
    if(f) fclose(f);
    free(data); free(texture_map); texture_map=NULL;
    return result;
}
#endif
