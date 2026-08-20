/*
 * Raw relative mouse input for classic AmigaOS.
 *
 * The SDL 1.2 CGX backend shipped with this toolchain implements
 * amiga_WarpWMCursor() as an empty function.  Consequently SDL_WarpMouse()
 * cannot recenter the Intuition pointer and SDL relative motion stops at a
 * screen edge.  Install one input.device stream handler instead and collect
 * the original IECLASS_RAWMOUSE deltas. While captured, movement-only events
 * are hidden from Intuition after their deltas have been saved, keeping the
 * invisible system pointer inside the game window. Button events still pass.
 *
 * This is deliberately a C translation unit: including the NDK headers in a
 * UE C++ file causes BYTE/WORD typedef collisions.
 */

#ifdef __amigaos__

#include <exec/types.h>
#include <exec/io.h>
#include <exec/interrupts.h>
#include <exec/nodes.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <proto/exec.h>

#include <string.h>

struct AmigaRawMouseState
{
	volatile LONG dx;
	volatile LONG dy;
	volatile ULONG pressed;
	volatile ULONG released;
	volatile ULONG held;
	volatile LONG enabled;
};

static struct AmigaRawMouseState s_mouse;
static struct Interrupt          s_handler;
static struct MsgPort*           s_port;
static struct IOStdReq*          s_io;
static int                       s_installed;
static int                       s_install_reported;

extern void AmigaDebugLogf( const char* fmt, ... );

/*
 * input.device calls stream handlers with the event list in A0 and is_Data
 * in A1. Return the original head in D0. Button events remain in the chain;
 * movement-only events become NOPs after UE has consumed their deltas.
 */
static struct InputEvent* __saveds AmigaRawMouseHandler(
	register struct InputEvent* events __asm("a0"),
	register struct AmigaRawMouseState* state __asm("a1") )
{
	struct InputEvent* event;

	if( state->enabled )
	{
		for( event = events; event; event = event->ie_NextEvent )
		{
			if( event->ie_Class == IECLASS_RAWMOUSE )
			{
				UWORD code;
				ULONG button = 0;
				state->dx += (LONG)event->ie_X;
				state->dy += (LONG)event->ie_Y;

				code = event->ie_Code & ~IECODE_UP_PREFIX;
				if( code == IECODE_LBUTTON )      button = 1UL << 0;
				else if( code == IECODE_MBUTTON ) button = 1UL << 1;
				else if( code == IECODE_RBUTTON ) button = 1UL << 2;
				if( button )
				{
					if( event->ie_Code & IECODE_UP_PREFIX )
					{
						state->held &= ~button;
						state->released |= button;
					}
					else
					{
						state->held |= button;
						state->pressed |= button;
					}
				}

				/* UE consumed both motion and buttons; prevent the hidden
				   system pointer from clicking another window or Workbench. */
				event->ie_Class = IECLASS_NULL;
			}
		}
	}
	return events;
}

static int AmigaRawMouseInstall( void )
{
	if( s_installed )
		return 1;

	s_port = CreateMsgPort();
	if( !s_port )
	{
		AmigaDebugLogf( "[Amiga] RawMouse: CreateMsgPort failed" );
		return 0;
	}

	s_io = (struct IOStdReq*)CreateIORequest( s_port, sizeof(*s_io) );
	if( !s_io )
	{
		AmigaDebugLogf( "[Amiga] RawMouse: CreateIORequest failed" );
		DeleteMsgPort( s_port );
		s_port = 0;
		return 0;
	}

	if( OpenDevice( "input.device", 0, (struct IORequest*)s_io, 0 ) != 0 )
	{
		AmigaDebugLogf( "[Amiga] RawMouse: OpenDevice input.device failed" );
		DeleteIORequest( s_io );
		DeleteMsgPort( s_port );
		s_io = 0;
		s_port = 0;
		return 0;
	}

	memset( &s_handler, 0, sizeof(s_handler) );
	s_handler.is_Node.ln_Type = NT_INTERRUPT;
	/*
	 * Run before Intuition/SDL can transform or consume raw mouse reports.
	 */
	s_handler.is_Node.ln_Pri  = 127;
	s_handler.is_Node.ln_Name = (char*)"Unreal raw mouse";
	s_handler.is_Data         = (APTR)&s_mouse;
	s_handler.is_Code         = (VOID (*)())AmigaRawMouseHandler;

	s_io->io_Command = IND_ADDHANDLER;
	s_io->io_Data    = (APTR)&s_handler;
	if( DoIO( (struct IORequest*)s_io ) != 0 )
	{
		AmigaDebugLogf( "[Amiga] RawMouse: IND_ADDHANDLER failed error=%d",
			(int)s_io->io_Error );
		CloseDevice( (struct IORequest*)s_io );
		DeleteIORequest( s_io );
		DeleteMsgPort( s_port );
		s_io = 0;
		s_port = 0;
		return 0;
	}

	s_installed = 1;
	if( !s_install_reported )
	{
		AmigaDebugLogf( "[Amiga] RawMouse: input.device handler installed priority=127" );
		s_install_reported = 1;
	}
	return 1;
}

int AmigaRawMouseSetCapture( int enabled )
{
	if( enabled && !AmigaRawMouseInstall() )
		return 0;

	/*
	 * The stream handler may run asynchronously.  The critical section is
	 * only three aligned LONG stores, so keep interrupts disabled briefly.
	 */
	Disable();
	s_mouse.dx = 0;
	s_mouse.dy = 0;
	s_mouse.pressed = 0;
	s_mouse.released = 0;
	s_mouse.held = 0;
	s_mouse.enabled = enabled ? 1 : 0;
	Enable();
	return 1;
}

int AmigaRawMouseRead( int* dx, int* dy, unsigned int* pressed,
	unsigned int* released, unsigned int* held )
{
	LONG read_dx;
	LONG read_dy;
	ULONG read_pressed;
	ULONG read_released;

	if( !s_installed || !s_mouse.enabled )
	{
		*dx = 0;
		*dy = 0;
		*pressed = 0;
		*released = 0;
		*held = 0;
		return 0;
	}

	Disable();
	read_dx = s_mouse.dx;
	read_dy = s_mouse.dy;
	read_pressed = s_mouse.pressed;
	read_released = s_mouse.released;
	s_mouse.dx = 0;
	s_mouse.dy = 0;
	s_mouse.pressed = 0;
	s_mouse.released = 0;
	*held = (unsigned int)s_mouse.held;
	Enable();

	*dx = (int)read_dx;
	*dy = (int)read_dy;
	*pressed = (unsigned int)read_pressed;
	*released = (unsigned int)read_released;
	return read_dx != 0 || read_dy != 0 ||
		read_pressed != 0 || read_released != 0;
}

void AmigaRawMouseShutdown( void )
{
	if( !s_installed )
		return;

	AmigaRawMouseSetCapture( 0 );
	s_io->io_Command = IND_REMHANDLER;
	s_io->io_Data    = (APTR)&s_handler;
	DoIO( (struct IORequest*)s_io );

	CloseDevice( (struct IORequest*)s_io );
	DeleteIORequest( s_io );
	DeleteMsgPort( s_port );
	s_io = 0;
	s_port = 0;
	s_installed = 0;
}

#else

int AmigaRawMouseSetCapture( int enabled ) { (void)enabled; return 0; }
int AmigaRawMouseRead( int* dx, int* dy, unsigned int* pressed,
	unsigned int* released, unsigned int* held )
{
	*dx = 0;
	*dy = 0;
	*pressed = 0;
	*released = 0;
	*held = 0;
	return 0;
}
void AmigaRawMouseShutdown( void ) {}

#endif
