#ifndef UE_SURFACE_BATCH_H
#define UE_SURFACE_BATCH_H

// Statistics are scoped to single-TMU BSP passes, not all renderer GL calls.
struct FSurfaceBatchStats
{
	unsigned long Passes, Fans, Calls, InputVertices, SubmittedVertices;
};

// No deferred geometry or state changes: each call ends its last batch before
// returning to the caller's texture/blend/depth changes. Emit supplies the same
// per-vertex attributes used by the original fan path.
template<class PolyType, class EmitVertex, class DrawFan>
static void DrawSurfacePolys( PolyType* First, bool Batch,
	FSurfaceBatchStats* Stats, const EmitVertex& Emit, const DrawFan& Fan )
{
	// Conservative bound, well below mglChooseVertexBufferSize(4096).
	const int MaxVertices = 384; // Whole triangles only.
	int Pending = 0;
	if( Stats ) ++Stats->Passes;
	for( PolyType* Poly = First; Poly; Poly = Poly->Next )
	{
		if( Stats )
		{
			++Stats->Fans;
			Stats->InputVertices += Poly->NumPts;
		}
		if( !Batch )
		{
			Fan( Poly );
			if( Stats )
			{
				++Stats->Calls;
				Stats->SubmittedVertices += Poly->NumPts;
			}
			continue;
		}
		if( Stats && Poly->NumPts>=3 )
			Stats->SubmittedVertices += 3u * (Poly->NumPts-2);
		for( int Triangle = 1; Triangle + 1 < Poly->NumPts; ++Triangle )
		{
			if( Pending == MaxVertices )
			{
				glEnd();
				Pending = 0;
			}
			if( Pending == 0 )
			{
				glBegin( GL_TRIANGLES );
				if( Stats ) ++Stats->Calls;
			}
			Emit( Poly, 0 );
			Emit( Poly, Triangle );
			Emit( Poly, Triangle + 1 );
			Pending += 3;
		}
	}
	if( Pending ) glEnd();
}

#endif
