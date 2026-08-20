/*=============================================================================
	IpDrvPrivate.h: Unreal TCP/IP driver.
	Copyright 1997 Epic MegaGames, Inc. This software is a trade secret.

Revision history:
	* Created by Tim Sweeney.
=============================================================================*/

#ifdef PLATFORM_MSVC
#pragma warning( disable : 4201 )
#endif

#ifdef PLATFORM_WIN32
#include <windows.h>
#include <winsock.h>
#else
#ifdef PLATFORM_AMIGA
// AmiTCP's <sys/socket.h> chain pulls in <exec/types.h>, whose BYTE (signed
// char) and WORD collide with the engine's unsigned BYTE/WORD. Block it and
// supply the handful of OS types the socket headers reference. See the same
// guard in Core/Inc/SDL12Compat.h.
#ifndef EXEC_TYPES_H
#define EXEC_TYPES_H
#include <stdint.h>
typedef void*          APTR;
typedef long           LONG;
typedef unsigned long  ULONG;
typedef unsigned short UWORD;
typedef unsigned char  UBYTE;
typedef short          BOOL;
typedef char*          STRPTR;
#endif
#endif
#include <unistd.h>
#include <arpa/inet.h>
#ifndef PLATFORM_PSVITA
#include <net/if.h>
#include <sys/ioctl.h>
#endif
#include <netdb.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/socket.h>
#endif

#include <stdlib.h>
#include <string.h>

#include "Engine.h"
#include "UnNet.h"
#include "UnSocket.h"

/*-----------------------------------------------------------------------------
	Definitions.
-----------------------------------------------------------------------------*/

struct FIpAddr
{
	DWORD Addr;
	DWORD Port;
};

extern UBOOL GInitialized;

/*-----------------------------------------------------------------------------
	Public includes.
-----------------------------------------------------------------------------*/

#include "IpDrvClasses.h"

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
