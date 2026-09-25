/* Standalone UE1/PiStorm blend/display probe. No engine, SDL or game assets. */
#include <clib/minigl_open_protos.h>
#include <proto/minigl.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define W 640
#define H 480
#define TS 64
static unsigned char pixels[2][TS * TS * 4] = {{1}};
static char title[200] = "MiniGL blend probe";
static const char *names[] = {"off", "copy", "alpha", "add", "mod", "ue"};

/* Local bits: the precedence and state-delta logic match UE SetBlend,
 * not the numeric values of Unreal's PolyFlags. */
enum { OCCLUDE=1, MASKED=2, TRANSLUCENT=4, MODULATED=8, HIGHLIGHTED=16, INVISIBLE=32 };
static unsigned current_flags = OCCLUDE;
#define POOL_SIZE 24
static GLuint pool[POOL_SIZE] = {1};
static unsigned long uploaded[POOL_SIZE] = {1};
static unsigned char *compose = NULL;
static int upload_failed = 0;

/* UE SetTexture: lazy allocation, shared Compose upload storage, then filters.
 * Stage 8 additionally reallocates storage for every upload as a lifetime
 * stress test, not an exact copy of UE's growth-only allocation policy. */
static void cached_texture(int stage, int slot, unsigned long frame)
{
    int x, y, w, h;
    GLenum filter;
    slot %= POOL_SIZE;
    w = 32 << (slot % 4);
    h = 32 << ((slot / 4) % 4);
    glEnable(GL_TEXTURE_2D);
    if(!pool[slot]) glGenTextures(1, &pool[slot]);
    glBindTexture(GL_TEXTURE_2D, pool[slot]);
    if(!uploaded[slot] || (slot == (int)(frame % POOL_SIZE) && uploaded[slot] != frame)) {
        if(stage == 8 || !compose) {
            size_t bytes = stage == 8 ? (size_t)w * h * 4 : 256UL * 256UL * 4;
            unsigned char *next = realloc(compose, bytes);
            if(!next) { upload_failed = 1; return; }
            compose = next;
        }
        for(y = 0; y < h; ++y) for(x = 0; x < w; ++x) {
            unsigned char *p = compose + 4 * (y * w + x);
            p[0] = (unsigned char)(x + slot * 17 + frame);
            p[1] = (unsigned char)(y * 2 + slot * 29);
            p[2] = (unsigned char)(((x / 8) ^ (y / 8)) & 1 ? 220 : 40);
            p[3] = (slot % 3 == 1 && x < w / 4) ? 0 : 255;
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, compose);
        uploaded[slot] = frame;
    }
    filter = (slot & 1) ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, filter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, filter);
}

static void bind_surface_texture(int stage, int slot, GLuint original, unsigned long frame)
{
    if(stage >= 7) cached_texture(stage, slot, frame);
    else glBindTexture(GL_TEXTURE_2D, original);
}

static void ue_blend(unsigned flags)
{
    unsigned changed;
    if(!(flags & (TRANSLUCENT | MODULATED))) flags |= OCCLUDE;
    else if(flags & TRANSLUCENT) flags &= ~MASKED;
    changed = current_flags ^ flags;
    if(changed & (TRANSLUCENT | MODULATED | HIGHLIGHTED)) {
        glEnable(GL_BLEND);
        if(flags & TRANSLUCENT)
            glBlendFunc(GL_ONE, (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_CLASSIC) ? GL_ONE : GL_ONE_MINUS_SRC_COLOR);
        else if(flags & MODULATED)
            glBlendFunc(GL_DST_COLOR, (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_CLASSIC) ? GL_ZERO : GL_SRC_COLOR);
        else if(flags & HIGHLIGHTED) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        else { glDisable(GL_BLEND); glBlendFunc(GL_ONE, GL_ZERO); }
    }
    if(changed & INVISIBLE) {
        GLboolean show = !(flags & INVISIBLE);
        glColorMask(show, show, show, show);
    }
    if(changed & OCCLUDE) glDepthMask((flags & OCCLUDE) != 0);
    if(changed & MASKED) {
        if(flags & MASKED) glEnable(GL_ALPHA_TEST);
        else glDisable(GL_ALPHA_TEST);
    }
    current_flags = flags;
}

static void blend(int mode)
{
    if(mode == 0) { glDisable(GL_BLEND); return; }
    glEnable(GL_BLEND); /* Same order as UE SetBlend. */
    switch(mode) {
    case 1: glBlendFunc(GL_ONE, GL_ZERO); break;
    case 2: glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); break;
    case 3: glBlendFunc(GL_ONE, GL_ONE); break;
    case 4: glBlendFunc(GL_DST_COLOR, GL_ZERO); break;
    }
}

static void vertex(int i, float x, float y, float z, float size, float u)
{
    const int right = i == 1 || i == 2;
    const int bottom = i >= 2;
    glTexCoord2f(u + (right ? 1.0f : 0.0f), bottom ? 1.0f : 0.0f);
    glVertex3f(x + (right ? size : -size), y + (bottom ? size : -size), z);
}

static void quad(int triangles, float x, float y, float z, float size, float u)
{
    int i, t;
    if(triangles) {
        for(t = 1; t < 3; ++t) {
            glBegin(GL_TRIANGLES);
            vertex(0, x, y, z, size, u);
            vertex(t, x, y, z, size, u);
            vertex(t + 1, x, y, z, size, u);
            glEnd();
        }
    } else {
        glBegin(GL_TRIANGLE_FAN);
        for(i = 0; i < 4; ++i) vertex(i, x, y, z, size, u);
        glEnd();
    }
}

static void surface(int stage, int number, int triangles,
                    float x, float y, float z, float size, float u)
{
    float positions[16][3], uv[16][2];
    int i, t, vertices = 3 + number % 14;
    if(stage != 5 && stage != 6) { quad(triangles, x, y, z, size, u); return; }
    /* Convex planar polygons. Stage 6 alone enlarges/tilts the same polygon
     * across side/near/eye planes; no invalid coordinates or concave fans. */
    if(stage == 6) size *= 4.0f;
    for(i = 0; i < vertices; ++i) {
        float angle = i * (6.28318530718f / vertices);
        float dx = cosf(angle) * size, dy = sinf(angle) * size;
        positions[i][0] = x + dx;
        positions[i][1] = y + dy;
        positions[i][2] = z + (stage == 6 ? dx * 2.5f : 0);
        uv[i][0] = u + cosf(angle) * 0.5f + 0.5f;
        uv[i][1] = sinf(angle) * 0.5f + 0.5f;
    }
    if(triangles) {
        for(t = 1; t < vertices - 1; ++t) {
            int indices[3] = {0, t, t + 1};
            glBegin(GL_TRIANGLES);
            for(i = 0; i < 3; ++i) {
                int k = indices[i];
                glTexCoord2f(uv[k][0], uv[k][1]);
                glVertex3f(positions[k][0], positions[k][1], positions[k][2]);
            }
            glEnd();
        }
    } else {
        glBegin(GL_TRIANGLE_FAN);
        for(i = 0; i < vertices; ++i) {
            glTexCoord2f(uv[i][0], uv[i][1]);
            glVertex3f(positions[i][0], positions[i][1], positions[i][2]);
        }
        glEnd();
    }
}

/* Synthetic surfaces, but UE's cached blend transitions and same-depth passes.
 * Each stage adds one class of work; ordinary test modes remain unchanged. */
static void sequence(int stage, int count, int triangles, GLuint *textures, float time, unsigned long frame)
{
    int n;
    ue_blend(OCCLUDE);
    glBindTexture(GL_TEXTURE_2D, textures[0]);
    glColor4f(1, 1, 1, 1);
    quad(triangles, 0, 0, 5, 4, time * 0.1f);
    for(n = 0; n < count; ++n) {
        unsigned flags = (n & 1) ? MASKED : OCCLUDE;
        float x = sinf(time + n * 0.43f) * 2.0f;
        float y = cosf(time * 0.7f + n * 0.61f);
        float z = 3.0f + (n % 7) * 0.08f;
        ue_blend(flags);
        bind_surface_texture(stage, n * 3, textures[n & 1], frame);
        if(stage == 4) /* Upload while previous primitives may still be queued. */
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TS, TS, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, pixels[n & 1]);
        glColor4f(1, 1, 1, 1);
        surface(stage, n, triangles, x, y, z, 0.35f, time * 0.1f);
        if(stage >= 2) {
            ue_blend(MODULATED);
            glEnable(GL_BLEND);
            glBlendFunc(GL_DST_COLOR, GL_ZERO); /* UE Amiga lightmap override. */
            if(flags & MASKED) glDepthFunc(GL_EQUAL);
            bind_surface_texture(stage, n * 3 + 1, textures[0], frame);
            surface(stage, n, triangles, x, y, z, 0.35f, 0);
            glDepthFunc(GL_LEQUAL);
        }
        if(stage >= 3) {
            ue_blend(HIGHLIGHTED);
            if(flags & MASKED) glDepthFunc(GL_EQUAL);
            bind_surface_texture(stage, n * 3 + 2, textures[1], frame);
            surface(stage, n, triangles, x, y, z, 0.35f, 0);
            glDepthFunc(GL_LEQUAL);
        }
        ue_blend(TRANSLUCENT);
        bind_surface_texture(stage, n * 3 + 2, textures[1], frame);
        surface(stage, n, triangles, x, y + 0.25f, z - 0.1f, 0.15f, 0);
        ue_blend(HIGHLIGHTED);
        surface(stage, n, triangles, x + 0.2f, y, z - 0.2f, 0.15f, 0);
    }
}

int main(int argc, char **argv)
{
    int mode = 1, triangles = 0, lock = 1, readback = 0, depthonly = 0;
    int nearest = 0, i, j, x, y, running = 1, context = 0, result = 1;
    int stage = 0, count = 32;
    unsigned long frames = 0, swaps = 0, changes = 0, hash = 0, previous = 0;
    GLenum error = GL_NO_ERROR;
    GLuint textures[2] = {0, 0};
    struct Window *window = NULL;
    for(i = 1; i < argc; ++i) {
        if(!strncmp(argv[i], "-mode=", 6)) {
            for(j = 0; j < 6 && strcmp(argv[i] + 6, names[j]); ++j) {}
            if(j == 6) goto usage;
            mode = j;
        } else if(!strncmp(argv[i], "-stage=", 7)) {
            char *end;
            long value = strtol(argv[i] + 7, &end, 10);
            if(*end || end == argv[i] + 7 || value < 1 || value > 8) goto usage;
            stage = (int)value;
        } else if(!strncmp(argv[i], "-count=", 7)) {
            char *end;
            long value = strtol(argv[i] + 7, &end, 10);
            if(*end || end == argv[i] + 7 || value < 1 || value > 2048) goto usage;
            count = (int)value;
        } else if(!strcmp(argv[i], "-tri")) triangles = 1;
        else if(!strcmp(argv[i], "-readback")) readback = 1;
        else if(!strcmp(argv[i], "-depthonly")) depthonly = 1;
        else if(!strcmp(argv[i], "-nearest")) nearest = 1;
        else if(!strcmp(argv[i], "-lock=default")) lock = 0;
        else if(!strcmp(argv[i], "-lock=smart")) lock = 1;
        else if(!strcmp(argv[i], "-lock=manual")) lock = 2;
        else if(!strcmp(argv[i], "-lock=auto")) lock = 3;
        else goto usage;
    }
    if(!MiniGLOpen()) { puts("Cannot open minigl.library"); return 1; }
    mglChooseWindowMode(GL_TRUE);
    mglChooseNumberOfBuffers(2);
    mglChoosePixelDepth(16);
    mglChooseVertexBufferSize(4096);
    if(!mglCreateContext(0, 0, W, H)) {
        fputs("MGL probe: cannot create 640x480 window/context\n", stderr);
        goto cleanup;
    }
    context = 1;
    memset(pool, 0, sizeof(pool));
    memset(uploaded, 0, sizeof(uploaded));
    upload_failed = 0;
    if(lock == 1) mglLockMode(MGL_LOCK_SMART);
    if(lock == 2) mglLockMode(MGL_LOCK_MANUAL);
    if(lock == 3) mglLockMode(MGL_LOCK_AUTOMATIC);
    mglEnableSync(GL_FALSE);
    window = (struct Window *)mglGetInputWindowHandle();
    if(!window) window = (struct Window *)mglGetWindowHandle();
    if(!window) {
        fputs("MGL probe: context has no window handle\n", stderr);
        goto cleanup;
    }
    /* MiniGL opens the window without IDCMP. ModifyIDCMP creates UserPort. */
    if(!ModifyIDCMP(window, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY) || !window->UserPort) {
        fputs("MGL probe: cannot enable window events\n", stderr);
        goto cleanup;
    }

    for(j = 0; j < 2; ++j) {
        for(y = 0; y < TS; ++y) for(x = 0; x < TS; ++x) {
            unsigned char *p = pixels[j] + 4 * (y * TS + x);
            int c = ((x / 8) ^ (y / 8)) & 1;
            p[0] = c ? 240 : 40;
            p[1] = j ? 70 : (c ? 190 : 40);
            p[2] = j ? 210 : (c ? 40 : 170);
            p[3] = j ? (x < 16 ? 0 : 160) : 255;
        }
        glGenTextures(1, &textures[j]);
        glBindTexture(GL_TEXTURE_2D, textures[j]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, nearest ? GL_NEAREST : GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, TS, TS, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels[j]);
    }
    glViewport(0, 0, W, H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glFrustum(-1.333333, 1.333333, -1, 1, 1, 100);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glScalef(1, -1, -1); /* UE camera coordinates. */
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glShadeModel(GL_SMOOTH);
    glAlphaFunc(GL_GREATER, 0.5f);
    glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
    glDisable(GL_BLEND);
    glDisable(GL_ALPHA_TEST);
    glDepthMask(GL_TRUE);
    current_flags = OCCLUDE;

    while(running) {
        struct IntuiMessage *msg;
        float time;
        while((msg = (struct IntuiMessage *)GetMsg(window->UserPort))) {
            ULONG cls = msg->Class;
            UWORD key = msg->Code;
            ReplyMsg((struct Message *)msg);
            if(cls == IDCMP_CLOSEWINDOW) running = 0;
            if(cls == IDCMP_RAWKEY && !(key & 0x80)) {
                if(key == 0x45) running = 0; /* Escape */
                if(key == 0x40 && !stage) { mode = (mode + 1) % 6; frames = swaps = changes = 0; } /* Space */
                if(key == 0x42) { triangles = !triangles; frames = swaps = changes = 0; } /* Tab */
            }
        }
        if(!running) break;
        ++frames;
        if(lock == 2 && !mglLockDisplay()) {
            snprintf(title, sizeof(title), "MGL probe: lock failed frame=%lu", frames);
            SetWindowTitles(window, title, (STRPTR)-1);
            break;
        }
        time = (float)(frames % 6283) * 0.01f;
        if(stage) ue_blend(OCCLUDE);
        else {
            glDisable(GL_BLEND);
            glDisable(GL_ALPHA_TEST);
            glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
            glDepthMask(GL_TRUE);
        }
        glDepthFunc(GL_LEQUAL);
        glClearColor(0.08f, 0.12f, 0.2f, 1);
        glClearDepth(1);
        glClear(GL_DEPTH_BUFFER_BIT | ((!depthonly || frames == 1) ? GL_COLOR_BUFFER_BIT : 0));
        glEnable(GL_TEXTURE_2D);
        glColor4f(1, 1, 1, 1);
        if(stage) sequence(stage, count, triangles, textures, time, frames);
        else {
        blend(mode == 5 ? 0 : mode);
        glBindTexture(GL_TEXTURE_2D, textures[0]);
        quad(triangles, 0, 0, 5, 4, time * 0.1f);
        if(mode == 5) { blend(4); glDepthMask(GL_FALSE); }
        glBindTexture(GL_TEXTURE_2D, textures[mode == 5 ? 0 : 1]);
        quad(triangles, sinf(time) * 1.5f, cosf(time * 0.7f) * 0.5f, 3, 0.8f, 0);
        if(mode == 5) {
            blend(3);
            glBindTexture(GL_TEXTURE_2D, textures[1]);
            quad(triangles, -sinf(time), 0.8f, 2.5f, 0.5f, 0);
            blend(0);
            glDepthMask(GL_TRUE);
            glEnable(GL_ALPHA_TEST);
            quad(triangles, 0, -0.8f, 2.5f, 0.5f, 0);
        }
        }
        if(upload_failed) {
            fputs("MGL probe: cannot allocate shared texture upload buffer\n", stderr);
            goto cleanup;
        }
        glFlush();
        if(readback) {
            unsigned char samples[4 * 4 * 3] = {0};
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(W / 2 - 2, H / 2 - 2, 4, 4, GL_RGB, GL_UNSIGNED_BYTE, samples);
            hash = 2166136261UL;
            for(i = 0; i < (int)sizeof(samples); ++i) hash = (hash ^ samples[i]) * 16777619UL;
            if(frames > 1 && hash != previous) ++changes;
            previous = hash;
        }
        if(lock == 2) mglUnlockDisplay();
        if((frames & 15) == 1) {
            snprintf(title, sizeof(title), "MGL %s stage=%d n=%d %s f=%lu swap=%lu before-swap", stage ? "seq" : names[mode], stage, count, triangles ? "tri" : "fan", frames, swaps);
            SetWindowTitles(window, title, (STRPTR)-1);
        }
        mglSwitchDisplay();
        ++swaps;
        if((frames & 15) == 1) {
            error = glGetError();
            snprintf(title, sizeof(title), "MGL %s stage=%d n=%d %s f=%lu swap=%lu returned err=%lx rb=%d changes=%lu",
                stage ? "seq" : names[mode], stage, count, triangles ? "tri" : "fan", frames, swaps, (unsigned long)error, readback, changes);
            SetWindowTitles(window, title, (STRPTR)-1);
        }
    }
    result = 0;
cleanup:
    if(context) {
        glFinish();
        mglUnlockDisplay();
        glDeleteTextures(2, textures);
        if(stage >= 7) glDeleteTextures(POOL_SIZE, pool);
        if(window) ModifyIDCMP(window, 0);
        mglDeleteContext();
    }
    MiniGLClose();
    free(compose);
    return result;
usage:
    puts("MGL-blend-test [-mode=off|copy|alpha|add|mod|ue] [-tri]\n"
         " [-stage=1|2|3|4|5|6|7|8] [-count=1..2048]\n"
         " [-lock=default|smart|manual|auto] [-readback] [-nearest] [-depthonly]\n"
         "Space: next blend mode; Tab: fan/triangles; Escape: quit.\n"
         "Defaults: copy blend, fan, smart locking, linear filtering. No log files.");
    return 1;
}
