/* Optional in-engine presentation probe. No MiniGL internals/layout assumptions.
 * mode 1: counters; 2: visible window; 3: GL back + window; 4: lock metadata too.
 * Readback and CGX reads can change scheduling/synchronization: compare modes.
 */
#include <stdio.h>
#include <string.h>
#include <intuition/intuition.h>
#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/minigl.h>
#define CYBERGFX_BASE_NAME ProbeCyberGfxBase
static struct Library *ProbeCyberGfxBase;
#include <proto/cybergraphics.h>

static int Mode = -1, Sample = -1, BackValid = -1, FrontValid = -1;
static unsigned long Attempts = 1, Swaps = 1, BackChanges = 1, FrontChanges = 1;
static unsigned long BackHash = 1, FrontHash = 1, PreviousBack = 1, PreviousFront = 1;
static int HadBack = -1, HadFront = -1, Equal = -1;
static unsigned long PriorError = 1, ReadError = 1;
static unsigned char Pixels[8*8*3] = {1};
static char Title[240] = "Unreal buffer probe";
static void *Address;
static unsigned long Pitch = 1;
static int LockOK = -1;

void AmigaMiniGLBufferProbeClose(void)
{
    if(Mode > 0 && ProbeCyberGfxBase) CloseLibrary(ProbeCyberGfxBase);
    ProbeCyberGfxBase = NULL;
    Mode = 0;
}

void AmigaMiniGLBufferProbeConfigure(int mode)
{
    AmigaMiniGLBufferProbeClose();
    Mode = mode >= 1 && mode <= 4 ? mode : 0;
    Attempts = Swaps = BackChanges = FrontChanges = 0;
    BackHash = FrontHash = PreviousBack = PreviousFront = 0;
    HadBack = HadFront = 0;
    Sample = BackValid = FrontValid = 0;
    Equal = -1; PriorError = ReadError = 0;
    Address = NULL; Pitch = 0; LockOK = -1;
    if(Mode >= 2) ProbeCyberGfxBase = OpenLibrary("cybergraphics.library", 39);
    if(Mode) fprintf(stderr, "MGL buffer probe %d: 8x8 centre sample every 16 swaps; backend=%lu\n",
                     Mode, (unsigned long)MiniGLDispatch->backendFlags);
}

static void phase(struct Window *window, const char *name)
{
    snprintf(Title, sizeof(Title),
        "UE buf%d a=%lu s=%lu B=%lu/%d F=%lu/%d eq=%d E=%lx/%lx %s",
        Mode, Attempts, Swaps, BackChanges, BackValid, FrontChanges, FrontValid,
        Equal, PriorError, ReadError, name);
    SetWindowTitles(window, Title, (STRPTR)-1);
}

void AmigaMiniGLBufferProbeBefore(struct Window *window)
{
    int w, h, i, row, untouched;
    GLint pack = 4;
    if(Mode <= 0 || !window) return;
    ++Attempts;
    Sample = (Attempts & 15) == 1;
    if(!Sample) return;
    BackValid = FrontValid = 0; Equal = -1;
    phase(window, "before-swap");
    if(Mode < 3) return;
    w = window->Width - window->BorderLeft - window->BorderRight;
    h = window->Height - window->BorderTop - window->BorderBottom;
    if(w < 8 || h < 8) return;
    if(Mode == 4) {
        MGLLockInfo info;
        memset(&info, 0, sizeof(info));
        phase(window, "lock-back");
        LockOK = mglLockBack(&info);
        Address = LockOK ? info.base_address : NULL;
        Pitch = LockOK ? info.pitch : 0;
        /* This mode deliberately changes lock state; never use it as baseline. */
        if(LockOK) mglUnlockDisplay();
    }
    phase(window, "read-back");
    PriorError = glGetError();
    glGetIntegerv(GL_PACK_ALIGNMENT, &pack);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    memset(Pixels, 0xa5, sizeof(Pixels));
    glReadPixels(w/2-4, h/2-4, 8, 8, GL_RGB, GL_UNSIGNED_BYTE, Pixels);
    ReadError = glGetError();
    glPixelStorei(GL_PACK_ALIGNMENT, pack);
    untouched = 1;
    for(i=0; i<(int)sizeof(Pixels); ++i) if(Pixels[i] != 0xa5) untouched = 0;
    BackValid = ReadError == GL_NO_ERROR && !untouched;
    if(BackValid) {
        BackHash = 2166136261UL;
        /* GL origin is bottom-left; hash top-to-bottom to match the window. */
        for(row=7; row>=0; --row)
            for(i=0; i<8*3; ++i) BackHash = (BackHash ^ Pixels[row*8*3+i])*16777619UL;
        if(HadBack && BackHash != PreviousBack) ++BackChanges;
        PreviousBack = BackHash; HadBack = 1;
    } else HadBack = 0;
    phase(window, "before-swap");
}

void AmigaMiniGLBufferProbeAfter(struct Window *window)
{
    int x, y, sx, sy, w, h;
    if(Mode <= 0 || !window) return;
    ++Swaps;
    if(!Sample) return;
    if(Mode >= 2 && ProbeCyberGfxBase) {
        w = window->Width - window->BorderLeft - window->BorderRight;
        h = window->Height - window->BorderTop - window->BorderBottom;
        sx = window->BorderLeft + w/2-4;
        sy = window->BorderTop + h-(h/2-4+8);
        phase(window, "read-window");
        FrontValid = w >= 8 && h >= 8;
        FrontHash = 2166136261UL;
        if(FrontValid) for(y=0; y<8; ++y) for(x=0; x<8; ++x) {
            ULONG rgb = ReadRGBPixel(window->RPort, sx+x, sy+y);
            if(rgb == 0xffffffffUL) FrontValid = 0;
            FrontHash = (FrontHash ^ ((rgb>>16)&255))*16777619UL;
            FrontHash = (FrontHash ^ ((rgb>>8)&255))*16777619UL;
            FrontHash = (FrontHash ^ (rgb&255))*16777619UL;
        }
        if(FrontValid) {
            if(HadFront && FrontHash != PreviousFront) ++FrontChanges;
            PreviousFront = FrontHash; HadFront = 1;
        } else HadFront = 0;
    }
    if(BackValid && FrontValid) Equal = BackHash == FrontHash;
    if(Mode == 4) {
        char details[64];
        snprintf(details, sizeof(details), "returned lock=%d ptr=%p pitch=%lu", LockOK, Address, Pitch);
        phase(window, details);
    } else phase(window, "returned");
}
