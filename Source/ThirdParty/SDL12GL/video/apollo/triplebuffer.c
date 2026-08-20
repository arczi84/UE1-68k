/*
 * triplebuffer.c
 *
 *  Created on: 10 lut 2021
 *      Author: arczi
 */
#ifndef ENABLE_TB
#define ENABLE_TB 1
#endif
#include "SDL_video.h"

ULONG *x_MemAddr1 = NULL;
ULONG *x_MemAddr2 = NULL;
ULONG *x_MemAddr3 = NULL;

ULONG *x_FBAddr1 = NULL;
ULONG *x_FBAddr2 = NULL;
ULONG *x_FBAddr3 = NULL;

#if ENABLE_TB

#include <exec/types.h>
#include <exec/memory.h>


#include "apolloammxenable.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SAGA_VIDEO_PLANEPTR       (0xDFF1EC)

static int curr_buffer = 0;

short ApolloTripleBuffer = FALSE;

ULONG GAME_memsize;



void InitBuffers1(SDL_Surface* surface)
{
	//if (Apollo_AMMXon)
	{
		//printf("init buffers\n");
		GAME_memsize = (surface->w * surface->h * 2) + 128;

		//printf("surface->w = %d surface->h = %d\n",surface->w, surface->h);

		// Aligned Drawing Buffers 1,2,3 */
		x_MemAddr1 = (ULONG*)AllocMem(GAME_memsize, MEMF_LOCAL | MEMF_FAST | MEMF_CLEAR);
		x_FBAddr1 = (ULONG*)((((ULONG)x_MemAddr1) + 31)& ~31);

		x_MemAddr2 = (ULONG*)AllocMem(GAME_memsize, MEMF_LOCAL | MEMF_FAST | MEMF_CLEAR);
		x_FBAddr2 = (ULONG*)((((ULONG)x_MemAddr2) + 31)& ~31);

		x_MemAddr3 = (ULONG*)AllocMem(GAME_memsize, MEMF_LOCAL | MEMF_FAST | MEMF_CLEAR);
		x_FBAddr3 = (ULONG*)((((ULONG)x_MemAddr3) + 31)& ~31);
	}

		//printf("START x_FBAddr1=%lu \n",x_FBAddr1 );
		//printf("START x_FBAddr2=%lu \n",x_FBAddr2 );
		//printf("START x_FBAddr3=%lu \n",x_FBAddr3 );

}

static short freed = 0;

void FreeBuffers1(void)
{

	if (Apollo_AMMXon)
		{
		//printf("freeing buffers..\n");
		if (freed == 0)
		   {
			FreeMem(x_MemAddr1, GAME_memsize);
			FreeMem(x_MemAddr2, GAME_memsize);
			FreeMem(x_MemAddr3, GAME_memsize);
			freed = 1;
			}
		}

	//printf("END x_FBAddr1=%lu \n",x_FBAddr1 );
	//printf("END x_FBAddr2=%lu \n",x_FBAddr2 );
	//printf("END x_FBAddr3=%lu \n",x_FBAddr3 );
}

void* ApolloFlip(SDL_Surface *surface)
//ushort* ApolloFlip(ushort* pixels)
{
	if	(Apollo_AMMXon)
	{
		switch (curr_buffer)
		{
		case 0:
			*(volatile ULONG*)SAGA_VIDEO_PLANEPTR = (ULONG) x_FBAddr1;
			surface->pixels = (ushort*)x_FBAddr2;
			break;
		case 1:
			*(volatile ULONG*)SAGA_VIDEO_PLANEPTR = (ULONG) x_FBAddr2;
			surface->pixels = (ushort*)x_FBAddr3;
			break;
		case 2:
			*(volatile ULONG*)SAGA_VIDEO_PLANEPTR = (ULONG) x_FBAddr3;
			surface->pixels = (ushort*)x_FBAddr1;
			break;
		}

		curr_buffer++;
		if (curr_buffer > 2)
			curr_buffer = 0;

		return surface->pixels;
	}
	else
		SDL_Flip(surface);

}
#else
void InitBuffers1(SDL_Surface* surface){}
void FreeBuffers1(void) {}
#endif


