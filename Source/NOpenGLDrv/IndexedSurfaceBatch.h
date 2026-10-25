#ifndef UE_INDEXED_SURFACE_BATCH_H
#define UE_INDEXED_SURFACE_BATCH_H

struct FIndexedSurfaceVertex { float X, Y, Z, U, V; };

// All work is completed before returning to a texture/blend/depth transition.
// Vertices are unique within each fan (no float-based/global deduplication).
template<class PolyType, class BuildVertex, class SubmitBatch, class DrawFan>
static void DrawIndexedSurfacePolys(PolyType* First, FSurfaceBatchStats* Stats,
    const BuildVertex& Build, const SubmitBatch& Submit, const DrawFan& Fan)
{
    enum { MaxVertices = 384, MaxIndices = 1152 };
    // Renderer is single-threaded; persistent storage keeps client pointers
    // valid after draw completion. Every submitted slot is overwritten.
    static FIndexedSurfaceVertex Vertices[MaxVertices];
    static unsigned short Indices[MaxIndices];
    int NumVertices = 0, NumIndices = 0;
    const auto Flush = [&]() {
        if (!NumIndices) return;
        Submit(Vertices, NumVertices, Indices, NumIndices);
        if (Stats) { ++Stats->Calls; Stats->SubmittedVertices += NumVertices; }
        NumVertices = NumIndices = 0;
    };
    if (Stats) ++Stats->Passes;
    for (PolyType* Poly = First; Poly; Poly = Poly->Next) {
        const int Count = Poly->NumPts;
        if (Stats) { ++Stats->Fans; Stats->InputVertices += Count; }
        if (Count < 3) continue;
        if (Count > MaxVertices) {
            Flush();
            Fan(Poly); // Preserve oversized polygons without truncating them.
            if (Stats) { ++Stats->Calls; Stats->SubmittedVertices += Count; }
            continue;
        }
        const int Needed = 3 * (Count - 2);
        if (NumVertices + Count > MaxVertices || NumIndices + Needed > MaxIndices)
            Flush();
        const unsigned short Base = (unsigned short)NumVertices;
        for (int i = 0; i < Count; ++i) Vertices[NumVertices++] = Build(Poly, i);
        for (int i = 1; i + 1 < Count; ++i) {
            Indices[NumIndices++] = Base;
            Indices[NumIndices++] = (unsigned short)(Base + i);
            Indices[NumIndices++] = (unsigned short)(Base + i + 1);
        }
    }
    Flush();
}
#endif
