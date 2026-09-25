/*------------------------------------------------------------------------------------
	Dependencies.
------------------------------------------------------------------------------------*/

#ifdef NOPENGLDRV_USE_MINIGL
// MiniGL's Amiga headers define a signed BYTE which collides with UE1's BYTE.
// Rename only the Amiga identifier while importing the v10 dispatch API.
#ifdef EXEC_TYPES_H
#undef EXEC_TYPES_H
#endif
#define BYTE NMiniGLAmigaByte
#include "MiniGLShared.h"
#undef BYTE

#define glActiveTexture glActiveTextureARB
#define glMultiTexCoord2f glMultiTexCoord2fARB
#define GL_TEXTURE0 GL_TEXTURE0_ARB
#define GL_BGRA GL_RGBA
#define GL_COLOR_INDEX8_EXT GL_COLOR_INDEX
#define GL_ADD GL_MODULATE
#define GL_COMBINE GL_MODULATE
#define GL_COMBINE_ALPHA GL_TEXTURE_ENV_MODE
#define GL_COMBINE_RGB GL_TEXTURE_ENV_MODE
#define GL_OPERAND0_ALPHA GL_TEXTURE_ENV_MODE
#define GL_PREVIOUS GL_TEXTURE_2D
#define GL_RGB_SCALE GL_TEXTURE_ENV_MODE
#define GL_SOURCE0_ALPHA GL_TEXTURE_ENV_MODE
#else
#include "glad.h"
#endif
#include "RenderPrivate.h"
#ifdef UE_MINIGL_HASHCACHE
#include "TextureBindCache.h"
#endif

/*------------------------------------------------------------------------------------
	OpenGL rendering private definitions.
------------------------------------------------------------------------------------*/

//
// Fixed function OpenGL renderer based on OpenGLDrv and XOpenGLDrv.
//
class DLL_EXPORT UNOpenGLRenderDevice : public URenderDevice
{
	DECLARE_CLASS_WITHOUT_CONSTRUCT(UNOpenGLRenderDevice, URenderDevice, CLASS_Config)

	// texture, lightmap, detail, fogmap
	static constexpr INT MaxTexUnits = 4;

	// Options.
	UBOOL NoFiltering;
	UBOOL UseHwPalette;
	UBOOL UseBGRA;
	UBOOL DetailTextures;
	UBOOL UseMultiTexture;
	UBOOL AutoFOV;
	UBOOL UseWindowBrightness;
	INT SwapInterval;

	// All currently cached textures.
	struct FCachedTexture
	{
		GLuint Id;
		INT BaseMip;
		INT MaxLevel;
		GLenum AppliedMinFilter, AppliedMagFilter;
	};
#ifdef UE_MINIGL_HASHCACHE
	TTextureBindCache<FCachedTexture> BindMap;
#else
	TMap<QWORD, FCachedTexture> BindMap;
#endif
	TArray<GLuint> TexAlloc;

	struct FTexInfo
	{
		QWORD CurrentCacheID;
		FLOAT UMult;
		FLOAT VMult;
		FLOAT UPan;
		FLOAT VPan;
	} TexInfo[MaxTexUnits];

	// Texture upload buffer;
	BYTE* Compose;
	DWORD ComposeSize;

	// Timing.
	INT BindCycles, ImageCycles, ComplexCycles, GouraudCycles, TileCycles;

	// Current state.
	FLOAT CurrentBrightness;
	DWORD CurrentPolyFlags;
	INT TextureUploadSemantic; // 0=regular, 1=lightmap, 2=fogmap
	FLOAT RProjZ, Aspect;
	FLOAT RFX2, RFY2;
	FPlane ColorMod;

	struct FCachedSceneNode
	{
		FLOAT FovAngle;
		FLOAT FX, FY;
		INT X, Y;
		INT XB, YB;
		INT SizeX, SizeY;
	} CurrentSceneNode;
#ifdef NOPENGLDRV_USE_MINIGL
	BYTE TextureFilter; // 0=Nearest, 1=Bilinear; this port uploads level zero only.
#endif

	// Constructors.
	UNOpenGLRenderDevice();
	static void InternalClassInitializer( UClass* Class );

	// URenderDevice interface.
	virtual UBOOL Init( UViewport* InViewport ) override;
	virtual void Exit() override;
	virtual void PostEditChange() override;
	virtual void Flush() override;
	virtual UBOOL Exec( const char* Cmd, FOutputDevice* Out ) override;
	virtual void Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* InHitData, INT* InHitSize ) override;
	virtual void Unlock( UBOOL Blit ) override;
	virtual void DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet ) override;
	virtual void DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Texture, FTransTexture** Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* SpanBuffer ) override;
	virtual void DrawTile( FSceneNode* Frame, FTextureInfo& Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FSpanBuffer* Span, FLOAT Z, FPlane Light, FPlane Fog, DWORD PolyFlags ) override;
	virtual void EndFlash() override;
	virtual void GetStats( char* Result ) override;
	virtual void Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 ) override;
	virtual void Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2 ) override;
	virtual void PushHit( const BYTE* Data, INT Count ) override;
	virtual void PopHit( INT Count, UBOOL bForce ) override;
	virtual void ReadPixels( FColor* Pixels ) override;
	virtual void ClearZ( FSceneNode* Frame ) override;

	// UNOpenGLRenderDevice interface.
	void SetSceneNode( FSceneNode* Frame );
	void SetCachedTextureFilter( FCachedTexture* Bind, GLenum Min, GLenum Mag );
	UBOOL WantsNearest( const FTextureInfo& Info, DWORD PolyFlags ) const;
	void SetBlend( DWORD PolyFlags, UBOOL InverseOrder = false );
	void SetTexture( INT TMU, FTextureInfo& Info, DWORD PolyFlags, FLOAT PanBias );
	void ResetTexture( INT TMU );
	void UploadTexture( FTextureInfo& Info, UBOOL Masked, UBOOL NewTexture );
	void EnsureComposeSize( const DWORD NewSize );
	void ConvertTextureMipI8( const FMipmap* Mip, const FColor* Palette, const UBOOL Masked, BYTE*& UploadBuf, GLenum& UploadFormat, GLenum& InternalFormat );
	void ConvertTextureMipBGRA7777( const FMipmap* Mip, BYTE*& UploadBuf, GLenum& UploadFormat, GLenum& InternalFormat );
	void UpdateSwapInterval();

	void DrawComplexSurfaceMultiTex( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet );
	void DrawComplexSurfaceSingleTex( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet );
};
