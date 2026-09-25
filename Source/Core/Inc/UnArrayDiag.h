#ifndef UE_ARRAY_DIAG_H
#define UE_ARRAY_DIAG_H
#ifdef UE_ARRAY_DIAG
struct FArrayDiagContext
{
	char File[256], Object[256];
	FArchive* Archive;
	INT Offset, Size;
};
extern CORE_API FArrayDiagContext GArrayDiagContext;
struct FArrayDiagScope
{
	FArrayDiagContext Previous;
	FArrayDiagScope( const char* File, const char* Object, FArchive* Archive, INT Offset, INT Size )
	: Previous(GArrayDiagContext)
	{
		appStrncpy( GArrayDiagContext.File, File, 256 );
		appStrncpy( GArrayDiagContext.Object, Object, 256 );
		GArrayDiagContext.Archive = Archive;
		GArrayDiagContext.Offset = Offset;
		GArrayDiagContext.Size = Size;
	}
	~FArrayDiagScope() { GArrayDiagContext = Previous; }
};
#endif
#endif
