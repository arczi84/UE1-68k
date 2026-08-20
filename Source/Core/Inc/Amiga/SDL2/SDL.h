/*=============================================================================
	SDL2/SDL.h: SDL2 include redirect for Amiga 68k.

	The engine includes "SDL2/SDL.h" everywhere, but Amiga 68k only has
	SDL 1.2. This directory is placed on the include path ahead of any real
	SDL2 so those includes resolve here and pick up the compatibility shim
	instead, leaving the driver sources unmodified.
=============================================================================*/

#ifndef _AMIGA_SDL2_SDL_H_
#define _AMIGA_SDL2_SDL_H_

#include "SDL12Compat.h"

#endif
