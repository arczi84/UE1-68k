/*=============================================================================
	UnGcc.h: Unreal definitions for GCC/Clang running under Win32 or a POSIX OS.
=============================================================================*/

/*----------------------------------------------------------------------------
	Platform compiler definitions.
----------------------------------------------------------------------------*/

#ifdef PLATFORM_WIN32
#define __WIN32__	1
#endif

#ifndef PLATFORM_BIG_ENDIAN
	#define __INTEL__	1
	#define __INTEL_BYTE_ORDER__ 1
#else
	#define __INTEL__	0
	#define __INTEL_BYTE_ORDER__ 0
#endif

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef PLATFORM_WIN32
#include <minwindef.h>
#endif

/*----------------------------------------------------------------------------
	Platform specifics types and defines.
----------------------------------------------------------------------------*/

// Undo any Windows defines.
#undef BYTE
#undef CHAR
#undef WORD
#undef DWORD
#undef INT
#undef FLOAT
#undef MAXBYTE
#undef MAXWORD
#undef MAXDWORD
#undef MAXINT
#undef VOID
#undef CDECL

// Make sure HANDLE is defined.
#ifndef _WINDOWS_
	#define HANDLE void*
	#define HINSTANCE void*
#endif

// Sizes.
enum {DEFAULT_ALIGNMENT = 8 }; // Default boundary to align memory allocations on.
enum {CACHE_LINE_SIZE   = 32}; // Cache line size.

// Optimization macros.
#define DISABLE_OPTIMIZATION _Pragma("GCC push_options") \
	_Pragma("GCC optimize(\"O0\")")
#define ENABLE_OPTIMIZATION  _Pragma("GCC pop_options")

// Function type macros.
#define VARARGS  /* Functions with variable arguments */
#ifdef PLATFORM_WIN32
#define DLL_IMPORT __declspec(dllimport)  /* Import function from DLL */
#define DLL_EXPORT __declspec(dllexport)  /* Export function to DLL */
#define CDECL
#define STDCALL
#else
#define DLL_IMPORT
#define DLL_EXPORT
#define CDECL
#define STDCALL
#define __cdecl
#define __stdcall
#endif

#ifdef UNREAL_STATIC
#undef DLL_IMPORT
#define DLL_IMPORT
#endif

// Variable arguments.
#define GET_VARARGS(msg,fmt)	\
{	\
	va_list ArgPtr;	\
	va_start( ArgPtr, fmt );	\
	vsprintf( msg, fmt, ArgPtr );	\
	va_end( ArgPtr );	\
}
#define GET_VARARGSR(msg,fmt,result)	\
{	\
	va_list ArgPtr;	\
	va_start( ArgPtr, fmt );	\
	result = vsprintf( msg, fmt, ArgPtr );	\
	va_end( ArgPtr );	\
}

// Compiler name.
#ifdef _DEBUG
	#define COMPILER "Compiled with GCC (Debug)"
#else
	#define COMPILER "Compiled with GCC"
#endif

// Bitfield alignment.
#define GCC_PACK(n) __attribute__((packed, aligned(n)))
#define GCC_ALIGN(n) __attribute__((aligned(n)))

// Hidden attribute, used for GPackage.
#ifdef PLATFORM_WIN32
#define GCC_HIDDEN
#elif !defined(UNREAL_STATIC)
#define GCC_HIDDEN __attribute__((visibility("hidden")))
#else
#define GCC_HIDDEN
#endif

#define GCC_USED __attribute__((used))

// Unsigned base types.
typedef uint16_t _WORD;  // 16-bit signed.
typedef uint64_t QWORD;  // 64-bit unsigned.

// Signed base types.
typedef char     CHAR;   // 8-bit  signed.
typedef int16_t  SWORD;  // 16-bit signed.
typedef int64_t  SQWORD; // 64-bit signed.

// Other base types.
// NOTE: 32-bit base types use plain int/unsigned rather than int32_t/uint32_t.
// On gcc 6.5 int32_t==int, but on gcc 15 int32_t==long, which makes INT!=int
// and breaks every UE1 signature that mixes INT with int. int is 32-bit on
// m68k either way, so this is a no-op for 6.5 and fixes the 15 build.
// On 68k, GCC aligns int/float/long to only 2 bytes inside structs, but the
// engine's property layout (and the x86/MSVC /Zp4 ABI the .u packages assume)
// needs 4-byte alignment. Forcing aligned(4) on the base typedefs makes every
// field of these types lay out at 4-byte boundaries like x86 — matching the
// engine's computed property offsets — without -malign-int (which ICEs gcc6.5).
#ifdef PLATFORM_AMIGA
	#define UE_ALIGN4 __attribute__((aligned(4)))
#else
	#define UE_ALIGN4
#endif

typedef int      UBOOL UE_ALIGN4;  // Boolean 0 (false) or 1 (true).
typedef double   DOUBLE; // 64-bit IEEE double.

#ifndef PLATFORM_WIN32 // On Windows these are defined in minwindef.h.
// Unsigned base types.
typedef uint8_t  BYTE;   // 8-bit  unsigned.
typedef unsigned int DWORD UE_ALIGN4;  // 32-bit unsigned.
// Signed base types.
typedef int      INT UE_ALIGN4;    // 32-bit signed.
typedef int64_t __int64; // 64-bit signed.
// Other base types.
typedef float    FLOAT UE_ALIGN4;  // 32-bit IEEE floating point.
#endif

// If C++ exception handling is disabled, force guarding to be off.
#ifdef PLATFORM_NO_EXCEPTIONS
	#undef  DO_GUARD
	#undef  DO_SLOW_GUARD
	#define DO_GUARD 0
	#define DO_SLOW_GUARD 0
#endif

// Make sure characters are signed.
static_assert((char)-1 < 0, "char must be signed.");

// No VC++ asm.
#undef ASM
#define ASM 0

// FILE forward declaration.
#define USEEK_CUR SEEK_CUR
#define USEEK_END SEEK_END
#define USEEK_SET SEEK_SET

// Pathnames.
#ifdef PLATFORM_WIN32
#define PATH(s) s
#else
#define PATH(s) appUnixPath( s )
char* appUnixPath( const char* Path );
#endif

// NULL.
#ifndef NULL
#define NULL 0
#endif

// Platform-specific strings.
#ifdef PLATFORM_WIN32
#define LINE_TERMINATOR "\r\n"
#define PATH_SEPARATOR "\\"
#define DLLEXT ".dll"
#else
#define LINE_TERMINATOR "\n"
#define PATH_SEPARATOR "/"
#define DLLEXT ".so"
#endif

// Package implementation.
#ifdef UNREAL_STATIC
	#define IMPLEMENT_PACKAGE_PLATFORM(pkgname) \
		extern "C" {BYTE GCC_USED DLL_EXPORT GLoaded##pkgname;} \
		STATIC_EXPORT( GLoaded##pkgname, GLoaded##pkgname )
#elif defined(PLATFORM_WIN32)
	#define IMPLEMENT_PACKAGE_PLATFORM(pkgname) \
		extern "C" {HINSTANCE hInstance;} \
		INT __declspec(dllexport) __attribute__((stdcall)) DllMain( HINSTANCE hInInstance, DWORD Reason, void* Reserved ) \
		{ hInstance = hInInstance; return 1; }
#else
	#define IMPLEMENT_PACKAGE_PLATFORM(pkgname) \
		extern "C" {HINSTANCE hInstance;} \
		BYTE GLoaded##pkgname;
#endif

// Windows aliases for POSIX functions or types.
#ifndef PLATFORM_WIN32
#define _utime utime
#define _stat stat
#define _isnan(x) isnan(x)
#define stricmp(x, y) strcasecmp((x), (y))
#define strnicmp(x, y, n) strncasecmp((x), (y), (n))
#define _strnicmp(x, y, n) strncasecmp((x), (y), (n))
#endif

// Byte-swapping macros.
#if __INTEL_BYTE_ORDER__
	#define INTEL_ORDER16(x) (x)
	#define INTEL_ORDER32(x) (x)
#else
	#define INTEL_ORDER16(x) __builtin_bswap16(x)
	#define INTEL_ORDER32(x) __builtin_bswap32(x)
#endif

/*----------------------------------------------------------------------------
	Functions.
----------------------------------------------------------------------------*/

//
// CPU cycles, related to GSecondsPerCycle.
//
CORE_API DWORD appCycles();

//
// Seconds, arbitrarily based.
//
extern CORE_API DOUBLE GSecondsPerCycle;
CORE_API DOUBLE appSeconds();

//
// Sleep for seconds.
//
CORE_API void appSleep( FLOAT Sec );

//
// appGetGUID
//
void appGetGUID( void* GUID );

/*----------------------------------------------------------------------------
	Globals.
----------------------------------------------------------------------------*/

// System identification.
extern "C"
{
	extern HINSTANCE      hInstance;
#if __INTEL__
	extern CORE_API UBOOL GIsMMX;
	extern CORE_API UBOOL GIsPentiumPro;
	extern CORE_API UBOOL GIsKatmai;
	extern CORE_API UBOOL GIsK6;
	extern CORE_API UBOOL GIsK63D;
#else
	// Some of these are still used in SoftDrv.
	#define GIsMMX false
	#define GIsPentiumPro false
	#define GIsKatmai false
	#define GIsK6 false
	#define GIsK63D false
#endif
}

/*----------------------------------------------------------------------------
	The End.
----------------------------------------------------------------------------*/
