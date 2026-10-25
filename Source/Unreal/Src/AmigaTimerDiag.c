/* Timer diagnostic (UE_TIMER_DIAG only): counts how often the engine reads the
 * clock (SDL_GetTicks, linked with -Wl,--wrap=SDL_GetTicks) and how often it
 * makes the MiniGL calls that check the SMART display lock (glEnd,
 * glDrawArrays, glDrawElements).  The counters are reset when the timedemo
 * flyby starts and reported when it ends.  Normal builds compile nothing. */
#ifdef UE_TIMER_DIAG
#include <stdio.h>
#include <string.h>
#include <exec/types.h>
#include <libraries/minigl.h>
#include <libraries/minigl_dispatch.h>

#define UE_DIAG_CALLERS 32

static MGLDispatchTable DiagTable;
static const MGLDispatchTable *RealTable;

static ULONG CountTicks, CountEnd, CountArrays, CountElements;
static ULONG CountSwitch, CountLock, CountUnlock;
static void *CallerPc[UE_DIAG_CALLERS];
static ULONG CallerCount[UE_DIAG_CALLERS];
static ULONG CallerOther;

extern unsigned int __real_SDL_GetTicks(void);

unsigned int __wrap_SDL_GetTicks(void)
{
    void *pc = __builtin_return_address(0);
    int i;
    CountTicks++;
    for( i = 0; i < UE_DIAG_CALLERS; i++ )
    {
        if( CallerPc[i] == pc ) { CallerCount[i]++; break; }
        if( !CallerPc[i] ) { CallerPc[i] = pc; CallerCount[i] = 1; break; }
    }
    if( i == UE_DIAG_CALLERS )
        CallerOther++;
    return __real_SDL_GetTicks();
}

static void DiagEnd(GLcontext c) { CountEnd++; RealTable->GLEnd(c); }
static void DiagArrays(GLcontext c, GLenum m, GLint f, GLsizei n)
{ CountArrays++; RealTable->GLDrawArrays(c, m, f, n); }
static void DiagElements(GLcontext c, GLenum m, GLsizei n, GLenum t, const GLvoid *i)
{ CountElements++; RealTable->GLDrawElements(c, m, n, t, i); }
static void DiagSwitch(GLcontext c) { CountSwitch++; RealTable->MGLSwitchDisplay(c); }
static GLboolean DiagLock(GLcontext c) { CountLock++; return RealTable->MGLLockDisplay(c); }
static void DiagUnlock(GLcontext c) { CountUnlock++; RealTable->MGLUnlockDisplay(c); }

void UETimerDiagInstall(void)
{
    ULONG size;
    if( !MiniGLDispatch || MiniGLDispatch == &DiagTable )
        return;
    RealTable = MiniGLDispatch;
    size = RealTable->structSize;
    if( size > sizeof(DiagTable) )
        size = sizeof(DiagTable);
    memcpy( &DiagTable, RealTable, size );
    DiagTable.GLEnd = DiagEnd;
    DiagTable.GLDrawArrays = DiagArrays;
    DiagTable.GLDrawElements = DiagElements;
    DiagTable.MGLSwitchDisplay = DiagSwitch;
    DiagTable.MGLLockDisplay = DiagLock;
    DiagTable.MGLUnlockDisplay = DiagUnlock;
    MiniGLDispatch = &DiagTable;
}

void UETimerDiagReset(void)
{
    CountTicks = CountEnd = CountArrays = CountElements = 0;
    CountSwitch = CountLock = CountUnlock = 0;
    memset( CallerPc, 0, sizeof(CallerPc) );
    memset( CallerCount, 0, sizeof(CallerCount) );
    CallerOther = 0;
}

/* Writes the report to F; callers are offsets from UETimerDiagInstall so they
 * can be resolved against the map/nm output of the same executable. */
void UETimerDiagReport(FILE *F, unsigned long Frames)
{
    int i;
    if( !Frames ) Frames = 1;
    fprintf( F, "TimerDiag frames=%lu SwitchDisplay=%lu\n", Frames, CountSwitch );
    fprintf( F, "TimerDiag SDL_GetTicks=%lu (%.1f/frame)\n", CountTicks, (double)CountTicks / Frames );
    fprintf( F, "TimerDiag glEnd=%lu (%.1f/frame) glDrawArrays=%lu (%.1f/frame) glDrawElements=%lu (%.1f/frame)\n",
        CountEnd, (double)CountEnd / Frames, CountArrays, (double)CountArrays / Frames,
        CountElements, (double)CountElements / Frames );
    fprintf( F, "TimerDiag mglLockDisplay=%lu mglUnlockDisplay=%lu\n", CountLock, CountUnlock );
    fprintf( F, "TimerDiag base=%08lx (UETimerDiagInstall)\n", (unsigned long)UETimerDiagInstall );
    for( i = 0; i < UE_DIAG_CALLERS && CallerPc[i]; i++ )
        fprintf( F, "TimerDiag caller %+ld count=%lu\n",
            (long)((char *)CallerPc[i] - (char *)UETimerDiagInstall), CallerCount[i] );
    if( CallerOther )
        fprintf( F, "TimerDiag caller other count=%lu\n", CallerOther );
}
#endif
