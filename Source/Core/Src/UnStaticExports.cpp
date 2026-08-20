/*=============================================================================
	UnStaticExports.cpp: Dreamcast-specific routines.
=============================================================================*/

#ifndef UNREAL_STATIC
#error "This file is for static builds only."
#endif

#include "Core.h"

CORE_API FPackageExport* GExportsTable;

#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
#endif

CORE_API void* appGetStaticExport( const char* Name )
{
	FPackageExport* Iter = GExportsTable;
#ifdef PLATFORM_AMIGA
	int Count = 0;
#endif
	while( Iter )
	{
#ifdef PLATFORM_AMIGA
		if( Iter->Name == NULL )
		{
			AmigaDebugLogf( "[Amiga]   appGetStaticExport: NULL Name at entry %d (addr=%p)!", Count, (void*)Iter );
			return nullptr;
		}
		++Count;
#endif
#ifdef PLATFORM_AMIGA
		if( Count > 2000 )
		{
			AmigaDebugLogf( "[Amiga]   appGetStaticExport: CYCLE detected (>2000 entries), aborting" );
			return nullptr;
		}
#endif
		if( !appStrcmp( Name, Iter->Name ) )
			return Iter->Address;
		Iter = Iter->Next;
	}
	return nullptr;
}
