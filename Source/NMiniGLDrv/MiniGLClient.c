/* v27 dispatch client. UE uses the published v25 prefix, not the optional
 * v27 context-from-window/bitmap suffix. Validate the required prefix only. */
#include <stddef.h>
#include <stdio.h>
#include <proto/exec.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>

extern const MGLDispatchTable *MiniGLGetDispatchTableLVO(void);
static char OpenError[256] = "MiniGL has not been opened";

const char *UEMiniGLGetOpenError(void) { return OpenError; }

BOOL MiniGLOpen(void)
{
    const MGLDispatchTable *dispatch;
    const ULONG required = offsetof(MGLDispatchTable, MGLCreateContextFromWindow);
    if (MiniGLBase && MiniGLDispatch) return TRUE;
    if (!MiniGLBase) MiniGLBase = OpenLibrary(MINIGLNAME, MINIGL_VERSION);
    if (!MiniGLBase) {
        snprintf(OpenError, sizeof(OpenError), "OpenLibrary(minigl.library, %d) failed", MINIGL_VERSION);
        return FALSE;
    }
    dispatch = MiniGLGetDispatchTableLVO();
    if (!dispatch) {
        snprintf(OpenError, sizeof(OpenError), "minigl.library %u.%u returned no dispatch table",
                 MiniGLBase->lib_Version, MiniGLBase->lib_Revision);
    } else if (dispatch->abiVersion != MINIGL_DISPATCH_ABI_VERSION || dispatch->structSize < required) {
        snprintf(OpenError, sizeof(OpenError), "minigl.library %u.%u: ABI=%lu size=%lu; UE requires ABI=%lu size>=%lu",
                 MiniGLBase->lib_Version, MiniGLBase->lib_Revision,
                 dispatch->abiVersion, dispatch->structSize, MINIGL_DISPATCH_ABI_VERSION, required);
    } else {
        MiniGLDispatch = dispatch;
        OpenError[0] = 0;
        return TRUE;
    }
    CloseLibrary(MiniGLBase);
    MiniGLBase = NULL;
    MiniGLDispatch = NULL;
    return FALSE;
}

void MiniGLClose(void)
{
    MiniGLDispatch = NULL;
    if (MiniGLBase) { CloseLibrary(MiniGLBase); MiniGLBase = NULL; }
}
