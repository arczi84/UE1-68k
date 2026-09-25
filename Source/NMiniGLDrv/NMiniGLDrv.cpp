/*
 * MiniGL renderer for AmigaOS.
 *
 * The rendering implementation is deliberately shared with NOpenGLDrv. The
 * compile-time aliases give Unreal a distinct package and render-device class,
 * while NOPENGLDRV_USE_MINIGL selects MiniGL's direct-call API.
 */
#define NOpenGLDrv NMiniGLDrv
#define UNOpenGLRenderDevice UNMiniGLRenderDevice

#include "../NOpenGLDrv/NOpenGLDrv.cpp"
