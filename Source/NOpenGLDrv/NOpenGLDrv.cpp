#include "SDL2/SDL.h"
#ifndef NOPENGLDRV_USE_MINIGL
#include "glad.h"
#endif

#include "NOpenGLDrvPrivate.h"
#include "SurfaceBatch.h"

#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
extern INT GAmigaStartupTraceFrames;
extern INT GAmigaMiniGLTraceFrames;
extern DWORD GAmigaFrameSerial;
// Keep this in .data: libnix/-noixemul does not clear the executable BSS.
static INT GAmigaTextureTraceRemaining = 12;
static INT GAmigaLightmapTraceRemaining = 1;
static INT GAmigaLightmapBlendTraceRemaining = 1;
static INT GAmigaI8TraceRemaining = 8;
#ifdef NOPENGLDRV_USE_MINIGL
// Keep this in .data as well (non-zero initializer): some libnix builds do not
extern "C" void AmigaMiniGLEnableProbe( int Enabled );
extern "C" void AmigaMiniGLBufferProbeConfigure( int Mode );
extern "C" void AmigaMiniGLProbePhase( const char* Phase );
extern "C" void AmigaMiniGLSetFrameLock( int Enabled );
static INT GAmigaPerfMask = -1;
static INT GAmigaFilterCache = -1;
static INT GAmigaBatch = -1;
static INT GAmigaBatchStatsEnabled = -1;
static unsigned long GAmigaBatchFrames = 1;
static FSurfaceBatchStats GAmigaBatchStats = { 1, 1, 1, 1, 1 };
// clear executable BSS.  Zero is restored explicitly by Init().
static INT GAmigaMiniGLTestMode = -1;
static INT GAmigaBSPTestSurface = 1;
static GLuint GAmigaMiniGLTestTexture = 0xffffffffu;
static QWORD GAmigaLockedTextureCacheID = ~(QWORD)0;
static INT GAmigaMiniGLTestTextureUploaded = -1;
static BYTE GAmigaTinyTextureUpload[16] = { 1 };
static GLuint GAmigaCheckerExtraTextures[7] = { 0xffffffffu };
static INT GAmigaCheckerSwitchTest = -1;
static INT GAmigaCheckerSwitchIndex = -1;
static INT GAmigaCheckerUVTest = -1;
static INT GAmigaCheckerBlendTest = -1;
static INT GAmigaCheckerDisableBlendTest = -1;
static INT GAmigaCheckerOpaqueBlendTest = -1;
static INT GAmigaCheckerConstantBlendTest = -1;
static INT GAmigaDisableAllBlendTest = -1;
// Optional A/B test: use the same blend factors as a Classic-backend capture.
// Bit 1: translucent; bit 2: modulated. Never changes backend identity/locking.
static INT GAmigaBlendCompat = -1;
// -1: unchanged renderer; mask bits 1 translucent, 2 modulated,
// 4 highlighted, 8 initial/copy blend. Test 26 still overrides all of them.
static INT GAmigaBlendMask = -1;
static INT GAmigaCheckerTrianglesTest = -1;
static INT GAmigaFullTrianglesTest = -1;
static INT GAmigaCheckerDisableAlphaTest = -1;
static INT GAmigaCheckerDepthWriteTest = -1;
#endif
#endif

// Preserve each vertex's attributes and fan winding, with a bounded three-vertex
// MiniGL submission in test 28. This avoids the fan+blend path isolated by 25/27.
template<class EmitVertex>
static void DrawRendererFan( INT NumPts, const EmitVertex& Emit )
{
#ifdef NOPENGLDRV_USE_MINIGL
	if( GAmigaFullTrianglesTest == 1 )
	{
		for( INT Triangle = 1; Triangle + 1 < NumPts; ++Triangle )
		{
			glBegin( GL_TRIANGLES );
			Emit( 0 );
			Emit( Triangle );
			Emit( Triangle + 1 );
			glEnd();
		}
		return;
	}
#endif
	glBegin( GL_TRIANGLE_FAN );
	for( INT i = 0; i < NumPts; ++i )
		Emit( i );
	glEnd();
}

template<class EmitVertex>
static void DrawRendererSurfacePolys( FSavedPoly* First, const EmitVertex& Emit )
{
#if defined(NOPENGLDRV_USE_MINIGL) && !defined(UE_MINIGL_STABLE_BASELINE)
	if( GAmigaMiniGLTestMode == 0 && GAmigaFullTrianglesTest == 0 )
	{
		DrawSurfacePolys( First, GAmigaBatch == 1, GAmigaBatchStatsEnabled ? &GAmigaBatchStats : NULL, Emit,
			[&]( FSavedPoly* Poly )
			{
				DrawRendererFan( Poly->NumPts, [&]( INT i ) { Emit( Poly, i ); } );
			} );
		return;
	}
#endif
	for( FSavedPoly* Poly = First; Poly; Poly = Poly->Next )
		DrawRendererFan( Poly->NumPts, [&]( INT i ) { Emit( Poly, i ); } );
}

static void EnableRendererBlend( DWORD PolyFlags = 0 )
{
#ifdef NOPENGLDRV_USE_MINIGL
	if( GAmigaDisableAllBlendTest == 1 )
	{
		glDisable( GL_BLEND );
		return;
	}
	if( GAmigaBlendMask >= 0 )
	{
		const INT Group = (PolyFlags & PF_Translucent) ? 1
			: (PolyFlags & PF_Modulated) ? 2
			: (PolyFlags & PF_Highlighted) ? 4 : 8;
		if( !(GAmigaBlendMask & Group) )
		{
			glDisable( GL_BLEND );
			return;
		}
	}
#endif
	glEnable( GL_BLEND );
}

/*-----------------------------------------------------------------------------
	Global implementation.
-----------------------------------------------------------------------------*/

#ifdef NOPENGLDRV_USE_MINIGL
IMPLEMENT_PACKAGE(NMiniGLDrv);
IMPLEMENT_CLASS(UNMiniGLRenderDevice);
#else
IMPLEMENT_PACKAGE(NOpenGLDrv);
IMPLEMENT_CLASS(UNOpenGLRenderDevice);
#endif

/*-----------------------------------------------------------------------------
	UNOpenGLRenderDevice implementation.
-----------------------------------------------------------------------------*/

// from XOpenGLDrv:
// PF_Masked requires index 0 to be transparent, but is set on the polygon instead of the texture,
// so we potentially need two copies of any palettized texture in the cache
// unlike in newer unreal versions the low cache bits are actually used, so we have use one of the
// actually unused higher bits for this purpose, thereby breaking 64-bit compatibility for now
#define MASKED_TEXTURE_TAG (1ULL << 60)

// FColor is adjusted for endianness
#define ALPHA_MASK 0xff000000

// lightmaps are 0-127
#define LIGHTMAP_SCALE 2

// and it also would be nice to overbright them
#define LIGHTMAP_OVERBRIGHT 1.4f

#define GL_CHECK_EXT(ext) GLAD_GL_ ## ext
#define GL_CHECK_VER(maj, min) (((maj) * 10 + (min)) <= (GLVersion.major * 10 + GLVersion.minor))

void UNOpenGLRenderDevice::InternalClassInitializer( UClass* Class )
{
	guardSlow(UNOpenGLRenderDevice::InternalClassInitializer);
#ifdef NOPENGLDRV_USE_MINIGL
	UEnum* Filters=new(Class,"EMiniGLTextureFilter",RF_Public)UEnum(NULL);
	Filters->Names.AddItem(FName("Nearest"));
	Filters->Names.AddItem(FName("Bilinear"));
	new(Class,"TextureFilter",RF_Public)UByteProperty(CPP_PROPERTY(TextureFilter),"Options",CPF_Config,Filters);
#else
	new(Class, "NoFiltering",         RF_Public)UBoolProperty( CPP_PROPERTY(NoFiltering),         "Options", CPF_Config );
#endif
	new(Class, "UseHwPalette",        RF_Public)UBoolProperty( CPP_PROPERTY(UseHwPalette),        "Options", CPF_Config );
	new(Class, "UseBGRA",             RF_Public)UBoolProperty( CPP_PROPERTY(UseBGRA),             "Options", CPF_Config );
	new(Class, "DetailTextures",      RF_Public)UBoolProperty( CPP_PROPERTY(DetailTextures),      "Options", CPF_Config );
	new(Class, "UseMultiTexture",     RF_Public)UBoolProperty( CPP_PROPERTY(UseMultiTexture),     "Options", CPF_Config );
	new(Class, "AutoFOV",             RF_Public)UBoolProperty( CPP_PROPERTY(AutoFOV),             "Options", CPF_Config );
	new(Class, "UseWindowBrightness", RF_Public)UBoolProperty( CPP_PROPERTY(UseWindowBrightness), "Options", CPF_Config );
	new(Class, "SwapInterval",        RF_Public)UIntProperty ( CPP_PROPERTY(SwapInterval),        "Options", CPF_Config );
	unguardSlow;
}

UNOpenGLRenderDevice::UNOpenGLRenderDevice()
{
#ifdef NOPENGLDRV_USE_MINIGL
	TextureFilter=1;
#endif
	for( INT Unit = 0; Unit < MaxTexUnits; ++Unit )
	{
		TexInfo[Unit].CurrentCacheID = 0;
		TexInfo[Unit].UMult = TexInfo[Unit].VMult = 0.f;
		TexInfo[Unit].UPan = TexInfo[Unit].VPan = 0.f;
	}
	NoFiltering = false;
	UseHwPalette = true;
	UseBGRA = true;
	DetailTextures = true;
	UseMultiTexture = true;
	AutoFOV = true;
	UseWindowBrightness = true;
	CurrentBrightness = -1.f;
	TextureUploadSemantic = 0;
	SwapInterval = 1;
#ifdef PLATFORM_AMIGA
	// QuarkTex handles filtered OpenGL 1.1 textures correctly.  Keep the
	// single-TMU compatibility path, but do not deliberately force the blocky
	// nearest-neighbour fallback now that the base renderer is stable.
	NoFiltering = false;
	UseHwPalette = false;
	UseBGRA = false;
	DetailTextures = true;
	UseMultiTexture = false;
	SwapInterval = 0;
	// Match the Linux renderer's high-quality feature set. These inherited
	// flags control whether UE1 even submits mirrors, fog, coronas and
	// high-detail actors to the render device.
	VolumetricLighting = true;
	ShinySurfaces = true;
	Coronas = true;
	HighDetailActors = true;
#ifdef NOPENGLDRV_USE_MINIGL
	// MiniGL v10 uses the stable single-texture OpenGL 1.1 path.
	// Keep bilinear filtering on: MiniGL supports GL_LINEAR, and forcing
	// GL_NEAREST here made every palettized (i.e. every world) texture blocky,
	// unlike the StormMESA reference.
	NoFiltering = false;
	DetailTextures = false;
#endif
#endif
}

UBOOL UNOpenGLRenderDevice::Init( UViewport* InViewport )
{
	guard(UNOpenGLRenderDevice::Init)

#ifdef NOPENGLDRV_USE_MINIGL
	GAmigaMiniGLTestMode = 0;
	GAmigaBatch = 0;
	GAmigaBatchStatsEnabled = Parse( appCmdLine(), "MGLBATCH=", GAmigaBatch );
#ifdef UE_MINIGL_STABLE_BASELINE
	if( GAmigaBatch )
		appErrorf( "This stable-baseline build does not include batching" );
	GAmigaBatchStatsEnabled = 0;
#endif
	if( GAmigaBatch<0 || GAmigaBatch>1 )
		appErrorf( "Use -mglbatch=0 or -mglbatch=1" );
	GAmigaBatchFrames = 0;
	appMemset( &GAmigaBatchStats, 0, sizeof(GAmigaBatchStats) );
	debugf( "MiniGL batch=%i: single-TMU BSP only, limit=384 vertices", GAmigaBatch );
	if(TextureFilter>1) TextureFilter=1;
	debugf("MiniGL TextureFilter=%s (level-zero min/mag)",TextureFilter ? "Bilinear" : "Nearest");
	GAmigaPerfMask = 0;
#ifndef UE_RELEASE_PACKAGE
	Parse( appCmdLine(), "MGLPERF=", GAmigaPerfMask );
#endif
	GAmigaPerfMask = Clamp( GAmigaPerfMask, 0, 3 );
#ifdef UE_MINIGL_FILTERCACHE
	GAmigaFilterCache = 1;
#else
	GAmigaFilterCache = 0;
#endif
	Parse( appCmdLine(), "MGLFILTERCACHE=", GAmigaFilterCache );
	if(GAmigaFilterCache<0 || GAmigaFilterCache>1)
		appErrorf("Use -mglfiltercache=0 or -mglfiltercache=1");
	GAmigaBlendCompat = 0;
	Parse( appCmdLine(), "MGLBLENDCOMPAT=", GAmigaBlendCompat );
	GAmigaBlendCompat = Clamp( GAmigaBlendCompat, 0, 3 );
	GAmigaBlendMask = -1;
	if( Parse( appCmdLine(), "MGLBLEND=", GAmigaBlendMask ) )
		GAmigaBlendMask = Clamp( GAmigaBlendMask, 0, 15 );
#ifdef UE_RELEASE_PACKAGE
	GAmigaMiniGLTestMode = 0;
#else
	Parse( appCmdLine(), "MGLTEST=", GAmigaMiniGLTestMode );
#endif
	GAmigaMiniGLTestMode = Clamp( GAmigaMiniGLTestMode, 0, 36 );
	if( GAmigaBatch && (GAmigaMiniGLTestMode || GAmigaPerfMask) )
		appErrorf( "Use -mglbatch=1 without -mgltest or -mglperf" );
	const UBOOL PresentationProbeTest = GAmigaMiniGLTestMode == 36;
	AmigaMiniGLEnableProbe( PresentationProbeTest );
	INT BufferProbeMode = 0;
#ifndef UE_RELEASE_PACKAGE
	Parse( appCmdLine(), "MGLBUFFER=", BufferProbeMode );
#endif
	AmigaMiniGLBufferProbeConfigure( BufferProbeMode );
	const UBOOL ManualLockTest = GAmigaMiniGLTestMode == 35;
	if( GAmigaPerfMask && GAmigaMiniGLTestMode )
		appErrorf("Use -mglperf without -mgltest to isolate Classic performance");
	GAmigaLockedTextureCacheID = 0;
	GAmigaMiniGLTestTextureUploaded = 0;
	AmigaDebugLogf( "[Amiga] MiniGL isolation test mode=%d", GAmigaMiniGLTestMode );
	// Test 19 follows exactly test 14's path, changing only MIN/MAG filtering.
	const UBOOL LinearCheckerTest = GAmigaMiniGLTestMode == 19;
	GAmigaCheckerSwitchTest = GAmigaMiniGLTestMode == 20;
	GAmigaCheckerUVTest = GAmigaMiniGLTestMode == 21;
	GAmigaCheckerBlendTest = (GAmigaMiniGLTestMode >= 22 && GAmigaMiniGLTestMode <= 24)
		|| (GAmigaMiniGLTestMode >= 31 && GAmigaMiniGLTestMode <= 34);
	GAmigaCheckerDisableBlendTest = GAmigaMiniGLTestMode == 23;
	GAmigaCheckerOpaqueBlendTest = GAmigaMiniGLTestMode == 24 || (GAmigaMiniGLTestMode >= 32 && GAmigaMiniGLTestMode <= 34);
	GAmigaCheckerDisableAlphaTest = GAmigaMiniGLTestMode == 33;
	GAmigaCheckerDepthWriteTest = GAmigaMiniGLTestMode == 34;
	GAmigaCheckerConstantBlendTest = GAmigaMiniGLTestMode == 25 || GAmigaMiniGLTestMode == 27 || GAmigaMiniGLTestMode == 30;
	GAmigaCheckerTrianglesTest = GAmigaMiniGLTestMode == 27 || (GAmigaMiniGLTestMode >= 30 && GAmigaMiniGLTestMode <= 34);
	GAmigaDisableAllBlendTest = GAmigaMiniGLTestMode == 26;
	GAmigaFullTrianglesTest = GAmigaMiniGLTestMode >= 28 && GAmigaMiniGLTestMode <= 34;
	if( GAmigaMiniGLTestMode == 29 )
		GAmigaMiniGLTestMode = 7; // Real BSP base textures and blend, triangles only.
	else if( GAmigaDisableAllBlendTest || GAmigaFullTrianglesTest || ManualLockTest || PresentationProbeTest )
		GAmigaMiniGLTestMode = 0;
	GAmigaCheckerSwitchIndex = 0;
	for( INT i = 0; i < 7; ++i )
		GAmigaCheckerExtraTextures[i] = 0;
	if( LinearCheckerTest || GAmigaCheckerSwitchTest || GAmigaCheckerUVTest || GAmigaCheckerBlendTest
		|| GAmigaCheckerConstantBlendTest )
		GAmigaMiniGLTestMode = 14;
	if( !MiniGLDispatch || !MiniGLDispatch->currentContext
		|| !*MiniGLDispatch->currentContext || !glGetString( GL_VERSION ) )
	{
		debugf( NAME_Warning, "Could not initialize native MiniGL context" );
		return false;
	}
	// Test 35 restores the classic MiniGL default left untouched by dethrace.
	// The native UE window explicitly selected SMART before renderer Init.
	// Keep normal fans, textures and blending to isolate that lock-mode override.
	if( GAmigaPerfMask && !(MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_CLASSIC) )
		appErrorf("-mglperf tests require the MiniGL Classic backend");
	AmigaMiniGLSetFrameLock( (GAmigaPerfMask & 1) != 0 );
	debugf("MiniGL perf=%i backend=%lu: frame-lock=%i filter-cache=%i",
		GAmigaPerfMask, (unsigned long)MiniGLDispatch->backendFlags,
		(GAmigaPerfMask & 1) != 0, (GAmigaPerfMask & 2) != 0 || GAmigaFilterCache == 1);
	debugf("MiniGL mglfiltercache=%i (per-texture min/mag cache)",GAmigaFilterCache);
	if( ManualLockTest )
		mglLockMode( MGL_LOCK_MANUAL );
	UseHwPalette = false;
	UseBGRA = false;
	UseMultiTexture = false;
#else
	if( !gladLoadGLLoader( &SDL_GL_GetProcAddress ) )
	{
		debugf( NAME_Warning, "Could not load GL: %s", SDL_GetError() );
		return false;
	}
#endif

	SupportsFogMaps = true;
	SupportsDistanceFog = true;

	UpdateSwapInterval();

#ifndef NOPENGLDRV_USE_MINIGL
	if( UseHwPalette && !GL_CHECK_EXT( EXT_paletted_texture ) )
	{
		debugf( NAME_Warning, "EXT_paletted_texture not available, disabling UseHwPalette" );
		UseHwPalette = false;
	}

	if( UseBGRA && !GL_CHECK_VER( 1, 2 ) && !GL_CHECK_EXT( EXT_bgra ) )
	{
		debugf( NAME_Warning, "EXT_bgra not available, disabling UseBGRA" );
		UseBGRA = false;
	}

	if( UseMultiTexture && ( !GL_CHECK_EXT( ARB_multitexture ) || !GL_CHECK_EXT( EXT_texture_env_combine ) ) )
	{
		debugf( NAME_Warning, "ARB_multitexture or EXT_texture_env_combine is not available, disabling UseMultiTexture" );
		UseMultiTexture = false;
	}

	if( UseMultiTexture )
	{
		GLint TMUnits;
		glGetIntegerv( GL_MAX_TEXTURE_UNITS_ARB, &TMUnits );
		if ( TMUnits < 4 )
		{
			debugf( NAME_Warning, "Not enough texture units (%i, expected 4), disabling UseMultiTexture", TMUnits );
			UseMultiTexture = false;
		}
	}

	debugf( NAME_Log, "Got OpenGL %d.%d", GLVersion.major, GLVersion.minor );
#else
	debugf( NAME_Log, "Got native MiniGL: %s", (const char*)glGetString( GL_VERSION ) );
#endif
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] GL identity: vendor='%s' renderer='%s' version='%s'",
		(const char*)glGetString( GL_VENDOR ), (const char*)glGetString( GL_RENDERER ),
		(const char*)glGetString( GL_VERSION ) );
#endif

	ComposeSize = 256 * 256 * 4;
	Compose = (BYTE*)appMalloc( ComposeSize, "GLComposeBuf" );
	verify( Compose );

	// Set modelview matrix to flip stuff into our coordinate system.
	const FLOAT Matrix[16] =
	{
		+1, +0, +0, +0,
		+0, -1, +0, +0,
		+0, +0, -1, +0,
		+0, +0, +0, +1,
	};
	glMatrixMode( GL_MODELVIEW );
	glLoadIdentity();
	glMultMatrixf( Matrix );

	// Set permanent state.
	glEnable( GL_DEPTH_TEST );
	glShadeModel( GL_SMOOTH );
	glAlphaFunc( GL_GREATER, 0.5 );
	glDisable( GL_ALPHA_TEST );
	glDepthMask( GL_TRUE );
	glBlendFunc( GL_ONE, GL_ZERO );
	EnableRendererBlend();
	glTexEnvf( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );

	CurrentPolyFlags = PF_Occlude;
	Viewport = InViewport;
	// A native fullscreen switch reuses this render device, but creates a new
	// GL context. Its viewport/projection are not the cached old context state.
	// Invalidate even when the window size and FOV did not change.
	SetSceneNode( NULL );

#ifdef NOPENGLDRV_USE_MINIGL
	GAmigaMiniGLTestTexture = 0;
	if( GAmigaMiniGLTestMode == 10 || GAmigaMiniGLTestMode == 14 || GAmigaMiniGLTestMode == 15 )
	{
		static const BYTE Checker[16] =
		{
			255, 255, 255, 255,   32,  32,  32, 255,
			 32,  32,  32, 255,  255, 255, 255, 255
		};
		glGenTextures( 1, &GAmigaMiniGLTestTexture );
		glBindTexture( GL_TEXTURE_2D, GAmigaMiniGLTestTexture );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, LinearCheckerTest ? GL_LINEAR : GL_NEAREST );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, LinearCheckerTest ? GL_LINEAR : GL_NEAREST );
		if( GAmigaMiniGLTestMode == 10 )
		{
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 2, 2, 0, GL_RGBA, GL_UNSIGNED_BYTE, Checker );
			GAmigaMiniGLTestTextureUploaded = 1;
		}
		else if( GAmigaMiniGLTestMode == 14 )
		{
			EnsureComposeSize( 64 * 64 * 4 );
			for( INT y = 0; y < 64; ++y )
				for( INT x = 0; x < 64; ++x )
				{
					BYTE* P = Compose + ( y * 64 + x ) * 4;
					const BYTE C = ( (x >> 3) ^ (y >> 3) ) & 1 ? 255 : 32;
					P[0] = P[1] = P[2] = C; P[3] = 255;
				}
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, Compose );
			GAmigaMiniGLTestTextureUploaded = 1;
			if( GAmigaCheckerSwitchTest )
			{
				// Seven extra copies plus the original: same pixels, filter and UVs.
				for( INT i = 0; i < 7; ++i )
				{
					glGenTextures( 1, &GAmigaCheckerExtraTextures[i] );
					glBindTexture( GL_TEXTURE_2D, GAmigaCheckerExtraTextures[i] );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
					glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
					glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, Compose );
				}
				glBindTexture( GL_TEXTURE_2D, GAmigaMiniGLTestTexture );
			}
		}
	}
#endif

	return true;
	unguard;
}

void UNOpenGLRenderDevice::Exit()
{
	guard(UNOpenGLRenderDevice::Exit);

	debugf( NAME_Log, "Shutting down OpenGL renderer" );
#ifdef NOPENGLDRV_USE_MINIGL
	if( GAmigaBatchStatsEnabled )
		debugf( "MiniGL batch summary: mode=%i frames=%lu passes=%lu fans=%lu calls=%lu inputverts=%lu submittedverts=%lu",
			GAmigaBatch, GAmigaBatchFrames, GAmigaBatchStats.Passes, GAmigaBatchStats.Fans,
			GAmigaBatchStats.Calls, GAmigaBatchStats.InputVertices, GAmigaBatchStats.SubmittedVertices );
#endif

	Flush();

#ifdef NOPENGLDRV_USE_MINIGL
	if( GAmigaMiniGLTestTexture != 0 && GAmigaMiniGLTestTexture != 0xffffffffu )
	{
		glDeleteTextures( 1, &GAmigaMiniGLTestTexture );
		GAmigaMiniGLTestTexture = 0;
	}
	if( GAmigaCheckerSwitchTest == 1 )
	{
		glDeleteTextures( 7, GAmigaCheckerExtraTextures );
		GAmigaCheckerSwitchTest = 0;
	}
#endif

	if( Compose )
	{
		appFree( Compose );
		Compose = NULL;
	}
	ComposeSize = 0;

	unguard;
}

void UNOpenGLRenderDevice::PostEditChange()
{
	guard(UNOpenGLRenderDevice::PostEditChange)

	Super::PostEditChange();

	UpdateSwapInterval();

	unguard;
}

void UNOpenGLRenderDevice::Flush()
{
	guard(UNOpenGLRenderDevice::Flush);

	if( TexAlloc.Num() )
	{
		debugf( NAME_Log, "Flushing %d/%d textures", TexAlloc.Num(), BindMap.Size() );
		for ( INT i = 0; i < MaxTexUnits; ++i )
		{
			ResetTexture( i );
		}
		glFinish();
		glDeleteTextures( TexAlloc.Num(), &TexAlloc(0) );
		TexAlloc.Empty();
		BindMap.Empty();
	}

	unguard;
}

UBOOL UNOpenGLRenderDevice::Exec( const char* Cmd, FOutputDevice* Out )
{
#ifdef NOPENGLDRV_USE_MINIGL
	if(ParseCommand(&Cmd,"GetTextureFiltering"))
	{
		Out->Log(TextureFilter ? "True" : "False");
		return true;
	}
	if(ParseCommand(&Cmd,"SetTextureFiltering"))
	{
		if(ParseCommand(&Cmd,"On")) TextureFilter=1;
		else if(ParseCommand(&Cmd,"Off")) TextureFilter=0;
		else { Out->Log("SetTextureFiltering: use On or Off"); return true; }
		SaveConfig();
		Out->Log(TextureFilter ? "True" : "False");
		return true;
	}
	if(ParseCommand(&Cmd,"MGLFILTER"))
	{
		char Mode[32];
		if(ParseToken(Cmd,Mode,sizeof(Mode),0))
		{
			if(!appStricmp(Mode,"Nearest")) TextureFilter=0;
			else if(!appStricmp(Mode,"Bilinear") || !appStricmp(Mode,"Linear")) TextureFilter=1;
			else { Out->Log("MGLFILTER: use Nearest or Bilinear"); return true; }
			SaveConfig();
		}
		Out->Logf("MiniGL TextureFilter=%s",TextureFilter ? "Bilinear" : "Nearest");
		return true;
	}
#endif
	return false;
}

void UNOpenGLRenderDevice::Lock( FPlane FlashScale, FPlane FlashFog, FPlane ScreenClear, DWORD RenderLockFlags, BYTE* InHitData, INT* InHitSize )
{
	guard(UNOpenGLRenderDevice::Lock);
#ifdef NOPENGLDRV_USE_MINIGL
	AmigaMiniGLProbePhase( "render-start" );
#endif
#ifdef PLATFORM_AMIGA
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=render-lock-enter", (unsigned long)GAmigaFrameSerial );
#endif

#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: RenderDevice Lock enter" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST RenderDevice Lock enter" );
	}
#endif

	BindCycles = ImageCycles = ComplexCycles = GouraudCycles = TileCycles = 0;

#ifdef NOPENGLDRV_USE_MINIGL
	if( GAmigaMiniGLTestMode == 6 )
		GAmigaBSPTestSurface = 1;
#endif

	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=before-glClearColor", (unsigned long)GAmigaFrameSerial );
#ifdef NOPENGLDRV_USE_MINIGL
	// Mode 1 deliberately alternates a plain GL clear.  If this also freezes,
	// no UE world/HUD/texture submission is involved: the failure is below the
	// renderer, in the clear/swap/display path.
	if( GAmigaMiniGLTestMode == 1 )
	{
		const UBOOL Odd = ( ( GAmigaFrameSerial >> 3 ) & 1 ) != 0;
		glClearColor( Odd ? 0.75f : 0.05f, 0.05f, Odd ? 0.05f : 0.75f, 1.f );
	}
	else
#endif
		glClearColor( ScreenClear.X, ScreenClear.Y, ScreenClear.Z, ScreenClear.W );
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=after-glClearColor", (unsigned long)GAmigaFrameSerial );
	glClearDepth( 1.0 );
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=after-glClearDepth", (unsigned long)GAmigaFrameSerial );
	glDepthFunc( GL_LEQUAL );
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=after-glDepthFunc", (unsigned long)GAmigaFrameSerial );

	if( UseWindowBrightness )
	{
		FLOAT TargetBrightness = CurrentBrightness;
		if ( Viewport && Viewport->Client )
			TargetBrightness = Viewport->Client->Brightness;
		else if ( CurrentBrightness < 0.f )
			TargetBrightness = 0.5f;
		if ( CurrentBrightness != TargetBrightness )
		{
			CurrentBrightness = TargetBrightness;
			const FLOAT Gamma = 0.5 + 1.5 * CurrentBrightness;
			SDL_Window* Window = (SDL_Window*)Viewport->GetWindow();
#ifndef NOPENGLDRV_USE_MINIGL
			SDL_SetWindowBrightness( Window, Gamma );
#else
			(void)Gamma;
			(void)Window;
#endif
		}
	}

	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=before-SetBlend", (unsigned long)GAmigaFrameSerial );
	SetBlend( PF_Occlude );
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=after-SetBlend", (unsigned long)GAmigaFrameSerial );

	GLbitfield ClearBits = GL_DEPTH_BUFFER_BIT;
	if( RenderLockFlags & LOCKR_ClearScreen )
		ClearBits |= GL_COLOR_BUFFER_BIT;
#ifdef NOPENGLDRV_USE_MINIGL
	if( GAmigaMiniGLTestMode == 1 )
		ClearBits |= GL_COLOR_BUFFER_BIT;
#endif
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=before-glClear bits=%lu", (unsigned long)GAmigaFrameSerial, (unsigned long)ClearBits );
	glClear( ClearBits );
#ifdef PLATFORM_AMIGA
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=after-clear", (unsigned long)GAmigaFrameSerial );
#endif

	if( FlashScale != FPlane(0.5f, 0.5f, 0.5f, 0.0f) || FlashFog != FPlane(0.0f, 0.0f, 0.0f, 0.0f) )
		ColorMod = FPlane( FlashFog.X, FlashFog.Y, FlashFog.Z, 1.f - Min( FlashScale.X * 2.f, 1.f ) );
	else
		ColorMod = FPlane( 0.f, 0.f, 0.f, 0.f );

	if( AutoFOV && Viewport && Viewport->Actor && Viewport->Actor->DesiredFOV == 90.0f )
	{
		const FLOAT Aspect = (FLOAT)Viewport->SizeX / (FLOAT)Viewport->SizeY;
		const FLOAT Fov = (FLOAT)( appAtan( appTan( 90.0 * PI / 360.0 ) * ( Aspect / ( 4.0 / 3.0 ) ) ) * 360.0 ) / PI;
		Viewport->Actor->DesiredFOV = Fov;
	}

#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: RenderDevice Lock leave" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST RenderDevice Lock leave" );
	}
#endif

	unguard;
}

void UNOpenGLRenderDevice::Unlock( UBOOL Blit )
{
	guard(UNOpenGLRenderDevice::Unlock);
#ifdef NOPENGLDRV_USE_MINIGL
	++GAmigaBatchFrames;
	AmigaMiniGLProbePhase( "before-flush" );
#endif
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: RenderDevice Unlock before GL completion" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST RenderDevice Unlock before GL completion" );
	}
#endif

#ifdef PLATFORM_AMIGA
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=before-glFlush", (unsigned long)GAmigaFrameSerial );
#endif
	glFlush();
#ifdef NOPENGLDRV_USE_MINIGL
	AmigaMiniGLProbePhase( "flush-returned" );
#endif
#ifdef PLATFORM_AMIGA
	if( GAmigaMiniGLTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=after-glFlush", (unsigned long)GAmigaFrameSerial );
#endif

#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: RenderDevice Unlock after GL completion" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST RenderDevice Unlock after GL completion" );
	}
#endif

	unguard;
}

void UNOpenGLRenderDevice::DrawComplexSurface( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet )
{
#ifdef NOPENGLDRV_USE_MINIGL
	// 1: clear/swap only, 2: canvas/HUD only, 4: actors only.
	if( GAmigaMiniGLTestMode == 1 || GAmigaMiniGLTestMode == 2 || GAmigaMiniGLTestMode == 4 )
		return;
#endif
	guard(UNOpenGLRenderDevice::DrawComplexSurface);

	check(Surface.Texture);

	SetSceneNode( Frame );

	uclock(ComplexCycles);

#ifdef NOPENGLDRV_USE_MINIGL
	// Mode 6 submits exactly the same BSP vertices, projection and depth as the
	// game, but without texture coordinates, texture uploads or multipass blend.
	// It separates bad geometry/transforms from texture and blend failures.
	if( GAmigaMiniGLTestMode == 6 )
	{
		ResetTexture( 0 );
		glDisable( GL_TEXTURE_2D );
		glDisable( GL_BLEND );
		glDisable( GL_ALPHA_TEST );
		glEnable( GL_DEPTH_TEST );
		glDepthMask( GL_TRUE );
		glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
		CurrentPolyFlags = PF_Occlude;
		const INT Color = GAmigaBSPTestSurface++ % 6;
		static const FLOAT TestColors[6][3] =
		{
			{ 1.00f, 0.15f, 0.15f }, { 0.15f, 1.00f, 0.15f },
			{ 0.15f, 0.30f, 1.00f }, { 1.00f, 1.00f, 0.15f },
			{ 1.00f, 0.15f, 1.00f }, { 0.15f, 1.00f, 1.00f }
		};
		glColor4f( TestColors[Color][0], TestColors[Color][1], TestColors[Color][2], 1.f );
		for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
		{
			glBegin( GL_TRIANGLE_FAN );
			for( INT i = 0; i < Poly->NumPts; ++i )
				glVertex3f( Poly->Pts[i]->Point.X, Poly->Pts[i]->Point.Y, Poly->Pts[i]->Point.Z );
			glEnd();
		}
		glEnable( GL_TEXTURE_2D );
		uunclock(ComplexCycles);
		return;
	}

	// Mode 10 uses one tiny immutable texture for every BSP surface.  It still
	// submits textured polygons and changing coordinates, but performs no UE1
	// texture-cache lookup, upload or per-surface texture switch.
	if( GAmigaMiniGLTestMode == 10 || GAmigaMiniGLTestMode == 14 || GAmigaMiniGLTestMode == 15 )
	{
		if( GAmigaMiniGLTestMode == 15 && !GAmigaMiniGLTestTextureUploaded )
		{
			EnsureComposeSize( 64 * 64 * 4 );
			for( INT y = 0; y < 64; ++y )
				for( INT x = 0; x < 64; ++x )
				{
					BYTE* P = Compose + ( y * 64 + x ) * 4;
					const BYTE C = ( (x >> 3) ^ (y >> 3) ) & 1 ? 255 : 32;
					P[0] = P[1] = P[2] = C; P[3] = 255;
				}
			glBindTexture( GL_TEXTURE_2D, GAmigaMiniGLTestTexture );
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, Compose );
			GAmigaMiniGLTestTextureUploaded = 1;
		}
		const FLOAT UDot = Facet.MapCoords.XAxis | Facet.MapCoords.Origin;
		const FLOAT VDot = Facet.MapCoords.YAxis | Facet.MapCoords.Origin;
		if( GAmigaCheckerBlendTest )
		{
			// Test 22 keeps test 14's texture and UVs, but uses real surface state.
			SetBlend( Surface.PolyFlags );
			// Test 23 preserves SetBlend calls, alpha/depth/color masks, but draws
			// with blending disabled. CurrentPolyFlags still tracks UE's flags.
			if( GAmigaCheckerDisableBlendTest )
				glDisable( GL_BLEND );
			// Test 24 keeps the blend enable state, but draws with copy factors.
			if( GAmigaCheckerOpaqueBlendTest )
				glBlendFunc( GL_ONE, GL_ZERO );
			// Test 33 differs from 32 only by disabling alpha testing for drawing.
			if( GAmigaCheckerDisableAlphaTest )
				glDisable( GL_ALPHA_TEST );
			// Test 34 differs from 32 only by keeping depth writes enabled.
			if( GAmigaCheckerDepthWriteTest )
				glDepthMask( GL_TRUE );
		}
		else
		{
			if( GAmigaCheckerConstantBlendTest )
			{
				// Test 25: fixed test-14 state, only blending is enabled (copy factors).
				glBlendFunc( GL_ONE, GL_ZERO );
				EnableRendererBlend();
			}
			else
				glDisable( GL_BLEND );
			glDisable( GL_ALPHA_TEST );
			glDepthMask( GL_TRUE );
			CurrentPolyFlags = PF_Occlude;
		}
		glEnable( GL_DEPTH_TEST );
		glEnable( GL_TEXTURE_2D );
		GLuint CheckerTexture = GAmigaMiniGLTestTexture;
		if( GAmigaCheckerSwitchTest )
		{
			const INT Index = GAmigaCheckerSwitchIndex;
			GAmigaCheckerSwitchIndex = ( Index + 1 ) % 8;
			if( Index != 0 )
				CheckerTexture = GAmigaCheckerExtraTextures[Index - 1];
		}
		glBindTexture( GL_TEXTURE_2D, CheckerTexture );
		glColor4f( 1.f, 1.f, 1.f, 1.f );
		FLOAT CheckerUMult = 1.f / 128.f, CheckerVMult = 1.f / 128.f;
		FLOAT CheckerUPan = 0.f, CheckerVPan = 0.f;
		if( GAmigaCheckerUVTest )
		{
			// Match SetTexture's base-pass UV normalization without its cache/GL state.
			const FTextureInfo& Info = *Surface.Texture;
			CheckerUMult = 1.f / (Info.UScale * static_cast<FLOAT>(Info.USize));
			CheckerVMult = 1.f / (Info.VScale * static_cast<FLOAT>(Info.VSize));
			CheckerUPan = Info.Pan.X + 0.f * Info.UScale;
			CheckerVPan = Info.Pan.Y + 0.f * Info.VScale;
		}
		for( FSavedPoly* Poly = Facet.Polys; Poly; Poly = Poly->Next )
		{
			if( GAmigaCheckerTrianglesTest )
			{
				if( GAmigaFullTrianglesTest )
				{
					// Test 30 controls the helper introduced in 28 against direct test 27.
					DrawRendererFan( Poly->NumPts, [&]( INT i )
					{
						const FVector& Point = Poly->Pts[i]->Point;
						const FLOAT U = Facet.MapCoords.XAxis | Point;
						const FLOAT V = Facet.MapCoords.YAxis | Point;
						glTexCoord2f( (U - UDot) * (1.f / 128.f), (V - VDot) * (1.f / 128.f) );
						glVertex3f( Point.X, Point.Y, Point.Z );
					} );
					continue;
				}
				// Match dethrace's immediate triangle submission. Same fan winding,
				// vertices, UVs and copy blend as test 25; at most three staged vertices.
				for( INT Triangle = 1; Triangle + 1 < Poly->NumPts; ++Triangle )
				{
					const INT Indices[3] = { 0, Triangle, Triangle + 1 };
					glBegin( GL_TRIANGLES );
					for( INT Corner = 0; Corner < 3; ++Corner )
					{
						const FVector& Point = Poly->Pts[Indices[Corner]]->Point;
						const FLOAT U = Facet.MapCoords.XAxis | Point;
						const FLOAT V = Facet.MapCoords.YAxis | Point;
						glTexCoord2f( (U - UDot) * (1.f / 128.f), (V - VDot) * (1.f / 128.f) );
						glVertex3f( Point.X, Point.Y, Point.Z );
					}
					glEnd();
				}
				continue;
			}
			glBegin( GL_TRIANGLE_FAN );
			for( INT i = 0; i < Poly->NumPts; ++i )
			{
				const FLOAT U = Facet.MapCoords.XAxis | Poly->Pts[i]->Point;
				const FLOAT V = Facet.MapCoords.YAxis | Poly->Pts[i]->Point;
				if( GAmigaCheckerUVTest )
					glTexCoord2f( (U-UDot-CheckerUPan)*CheckerUMult, (V-VDot-CheckerVPan)*CheckerVMult );
				else
					glTexCoord2f( (U - UDot) * (1.f / 128.f), (V - VDot) * (1.f / 128.f) );
				glVertex3f( Poly->Pts[i]->Point.X, Poly->Pts[i]->Point.Y, Poly->Pts[i]->Point.Z );
			}
			glEnd();
		}
		uunclock(ComplexCycles);
		return;
	}
#endif

	if( UseMultiTexture
#ifdef NOPENGLDRV_USE_MINIGL
		&& GAmigaMiniGLTestMode < 7
#endif
	)
	{
		// Draw with multitexture.
		DrawComplexSurfaceMultiTex( Frame, Surface, Facet );
	}
	else
	{
		// Draw with single texture unit.
		DrawComplexSurfaceSingleTex(Frame, Surface, Facet);
	}

	uunclock(ComplexCycles);

	unguard;
}

void UNOpenGLRenderDevice::DrawComplexSurfaceMultiTex( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet )
{
	const FLOAT UDot = Facet.MapCoords.XAxis | Facet.MapCoords.Origin;
	const FLOAT VDot = Facet.MapCoords.YAxis | Facet.MapCoords.Origin;

	SetBlend( Surface.PolyFlags );
	SetTexture( 0, *Surface.Texture, ( Surface.PolyFlags & PF_Masked ), 0.0 );

	if( Surface.LightMap )
	{
		SetTexture( 1, *Surface.LightMap, 0, -0.5f );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
		glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE );
		glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f );
		glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
		glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
		glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
	}

	if( Surface.DetailTexture && DetailTextures )
	{
		SetTexture( 2, *Surface.DetailTexture, 0, 0.f );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
		glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_MODULATE );
		glTexEnvf( GL_TEXTURE_ENV, GL_RGB_SCALE, 2.0f );
		glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
		glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
		glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
	}

	if( Surface.FogMap )
	{
		SetTexture( 3, *Surface.FogMap, 0, -0.5f );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE );
		glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_RGB, GL_ADD );
		glTexEnvi( GL_TEXTURE_ENV, GL_COMBINE_ALPHA, GL_REPLACE );
		glTexEnvi( GL_TEXTURE_ENV, GL_SOURCE0_ALPHA, GL_PREVIOUS );
		glTexEnvi( GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA );
	}

	glColor4f( 1.f, 1.f, 1.f, 1.f );
	for( FSavedPoly* Poly=Facet.Polys; Poly; Poly=Poly->Next )
	{
		DrawRendererFan( Poly->NumPts, [&]( INT i )
		{
			const FLOAT U = Facet.MapCoords.XAxis | Poly->Pts[i]->Point;
			const FLOAT V = Facet.MapCoords.YAxis | Poly->Pts[i]->Point;
			for( INT t=0; t<MaxTexUnits; ++t)
			{
				if( TexInfo[t].CurrentCacheID != 0 )
				{
					glMultiTexCoord2f( GL_TEXTURE0+t, (U-UDot-TexInfo[t].UPan)*TexInfo[t].UMult, (V-VDot-TexInfo[t].VPan)*TexInfo[t].VMult );
				}
			}
			glVertex3fv( &Poly->Pts[i]->Point.X );
		} );
	}

	for( INT t=1; t<MaxTexUnits; ++t)
	{
		ResetTexture( t );
	}
}

void UNOpenGLRenderDevice::DrawComplexSurfaceSingleTex( FSceneNode* Frame, FSurfaceInfo& Surface, FSurfaceFacet& Facet )
{
	const FLOAT UDot = Facet.MapCoords.XAxis | Facet.MapCoords.Origin;
	const FLOAT VDot = Facet.MapCoords.YAxis | Facet.MapCoords.Origin;

	// Draw texture.
	SetBlend( Surface.PolyFlags );
	SetTexture( 0, *Surface.Texture, ( Surface.PolyFlags & PF_Masked ), 0.f );
	glColor4f( 1.f, 1.f, 1.f, 1.f );
	DrawRendererSurfacePolys( Facet.Polys, [&]( FSavedPoly* Poly, INT i )
	{
		const FLOAT U = Facet.MapCoords.XAxis | Poly->Pts[i]->Point;
		const FLOAT V = Facet.MapCoords.YAxis | Poly->Pts[i]->Point;
		glTexCoord2f( (U-UDot-TexInfo[0].UPan)*TexInfo[0].UMult, (V-VDot-TexInfo[0].VPan)*TexInfo[0].VMult );
		glVertex3f( Poly->Pts[i]->Point.X, Poly->Pts[i]->Point.Y, Poly->Pts[i]->Point.Z );
	} );

#ifdef NOPENGLDRV_USE_MINIGL
	// These modes stop after the base texture: no lightmap/detail/fog pass.
	if( GAmigaMiniGLTestMode == 7 || GAmigaMiniGLTestMode == 9
		|| GAmigaMiniGLTestMode == 11 || GAmigaMiniGLTestMode == 12
		|| GAmigaMiniGLTestMode == 13 || GAmigaMiniGLTestMode == 16
		|| GAmigaMiniGLTestMode == 17 || GAmigaMiniGLTestMode == 18 )
		return;
#endif

	// Draw the static/dynamic lightmap as a second modulated pass.
	if( Surface.LightMap )
	{
		SetBlend( PF_Modulated );
#ifdef PLATFORM_AMIGA
		// QuarkTex/legacy AmigaMesa produces a black result for UE1's desktop
		// overbright equation (DST_COLOR, SRC_COLOR). Classic MiniGL may silently
		// replace that unsupported pair with alpha blending. Use the supported
		// one-times equation and fold UE1's overbright scale into the lightmap.
		EnableRendererBlend( PF_Modulated );
		glBlendFunc( GL_DST_COLOR, GL_ZERO );
#endif
		if( Surface.PolyFlags & PF_Masked )
			glDepthFunc( GL_EQUAL );
		TextureUploadSemantic = 1;
		SetTexture( 0, *Surface.LightMap, 0, -0.5 );
		TextureUploadSemantic = 0;
		glColor4f( 1.f, 1.f, 1.f, 1.f );
		DrawRendererSurfacePolys( Facet.Polys, [&]( FSavedPoly* Poly, INT i )
		{
			const FLOAT U = Facet.MapCoords.XAxis | Poly->Pts[i]->Point;
			const FLOAT V = Facet.MapCoords.YAxis | Poly->Pts[i]->Point;
			glTexCoord2f( (U-UDot-TexInfo[0].UPan)*TexInfo[0].UMult, (V-VDot-TexInfo[0].VPan)*TexInfo[0].VMult );
			glVertex3f( Poly->Pts[i]->Point.X, Poly->Pts[i]->Point.Y, Poly->Pts[i]->Point.Z );
		} );
#if defined(PLATFORM_AMIGA) && !defined(NOPENGLDRV_USE_MINIGL)
		if( GAmigaLightmapBlendTraceRemaining > 0 )
		{
			AmigaDebugLogf( "[Amiga] lightmap pass GL error=0x%04lx", (unsigned long)glGetError() );
			--GAmigaLightmapBlendTraceRemaining;
		}
		// Keep CurrentPolyFlags and the actual GL state synchronized for a
		// following detail-texture pass.
		glBlendFunc( GL_DST_COLOR, GL_SRC_COLOR );
#endif
		if( Surface.PolyFlags & PF_Masked )
			glDepthFunc( GL_LEQUAL );
	}

#ifdef PLATFORM_AMIGA
	// Do not let the last single-TMU overlay pass leak its depth function into
	// later world or canvas draws.  Blend/texture state is normalized at the
	// consumer because the next primitive may use a different UE poly style.
	glDepthFunc( GL_LEQUAL );
#endif

#ifdef NOPENGLDRV_USE_MINIGL
	// Mode 8 includes the lightmap, but excludes detail and fog overlays.
	if( GAmigaMiniGLTestMode == 8 )
		return;
#endif

	// Draw detail texture overlaid.
	if( Surface.DetailTexture && DetailTextures )
	{
		SetBlend( PF_Modulated );
		if( Surface.PolyFlags & PF_Masked )
			glDepthFunc( GL_EQUAL );
		SetTexture( 0, *Surface.DetailTexture, 0, 0.f );

		DrawRendererSurfacePolys( Facet.Polys, [&]( FSavedPoly* Poly, INT i )
		{
			const FLOAT U = Facet.MapCoords.XAxis | Poly->Pts[i]->Point;
			const FLOAT V = Facet.MapCoords.YAxis | Poly->Pts[i]->Point;
			glTexCoord2f( (U-UDot-TexInfo[0].UPan)*TexInfo[0].UMult, (V-VDot-TexInfo[0].VPan)*TexInfo[0].VMult );
			glVertex3f( Poly->Pts[i]->Point.X, Poly->Pts[i]->Point.Y, Poly->Pts[i]->Point.Z );
		} );
		if( Surface.PolyFlags & PF_Masked )
			glDepthFunc( GL_LEQUAL );
	}

	// Draw volumetric fog as an additive second pass.
	if( Surface.FogMap )
	{
		SetBlend( PF_Highlighted );
		if( Surface.PolyFlags & PF_Masked )
			glDepthFunc( GL_EQUAL );
		TextureUploadSemantic = 2;
		SetTexture( 0, *Surface.FogMap, 0, -0.5 );
		TextureUploadSemantic = 0;
		DrawRendererSurfacePolys( Facet.Polys, [&]( FSavedPoly* Poly, INT i )
		{
			const FLOAT U = Facet.MapCoords.XAxis | Poly->Pts[i]->Point;
			const FLOAT V = Facet.MapCoords.YAxis | Poly->Pts[i]->Point;
			glTexCoord2f( (U-UDot-TexInfo[0].UPan)*TexInfo[0].UMult, (V-VDot-TexInfo[0].VPan)*TexInfo[0].VMult );
			glVertex3f( Poly->Pts[i]->Point.X, Poly->Pts[i]->Point.Y, Poly->Pts[i]->Point.Z );
		} );
		if( Surface.PolyFlags & PF_Masked )
			glDepthFunc( GL_LEQUAL );
	}
}

void UNOpenGLRenderDevice::DrawGouraudPolygon( FSceneNode* Frame, FTextureInfo& Texture, FTransTexture** Pts, INT NumPts, DWORD PolyFlags, FSpanBuffer* SpanBuffer )
{
#ifdef NOPENGLDRV_USE_MINIGL
	// 1: clear/swap only, 2: canvas/HUD only, 3: BSP/world only.
	if( GAmigaMiniGLTestMode == 1 || GAmigaMiniGLTestMode == 2 || GAmigaMiniGLTestMode == 3
		|| GAmigaMiniGLTestMode == 6 || GAmigaMiniGLTestMode == 7 || GAmigaMiniGLTestMode == 8
		|| GAmigaMiniGLTestMode == 9 || GAmigaMiniGLTestMode == 10
		|| GAmigaMiniGLTestMode == 11 || GAmigaMiniGLTestMode == 12
		|| GAmigaMiniGLTestMode == 13 || GAmigaMiniGLTestMode == 14
		|| GAmigaMiniGLTestMode == 15 || GAmigaMiniGLTestMode == 16
		|| GAmigaMiniGLTestMode == 17 || GAmigaMiniGLTestMode == 18 )
		return;
#endif
		guard(UNOpenGLRenderDevice::DrawGouraudPolygon);

		SetSceneNode( Frame );
		uclock(GouraudCycles);
		SetBlend( PolyFlags );
		SetTexture( 0, Texture, ( PolyFlags & PF_Masked ), 0 );
#ifdef PLATFORM_AMIGA
		// QuarkTex state can have texturing disabled while our cache still says
		// this texture is current.  SetTexture then returns early and a mesh is
		// rendered as its bare Gouraud colour (white/cyan) until another bind.
		// Reassert the fixed-function texture state for every mesh polygon.
		glEnable( GL_TEXTURE_2D );
		glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
#endif
		ResetTexture( 1 );
		ResetTexture( 2 );
		ResetTexture( 3 );

		const UBOOL IsModulated = ( PolyFlags & PF_Modulated );

		if( IsModulated )
			glColor4f( 1.f, 1.f, 1.f, 1.f );

		DrawRendererFan( NumPts, [&]( INT i )
		{
			FTransTexture* P = Pts[i];
			if( !IsModulated )
				glColor4f( P->Light.X, P->Light.Y, P->Light.Z, 1.f );
			glTexCoord2f( P->U*TexInfo[0].UMult, P->V*TexInfo[0].VMult );
			glVertex3f( P->Point.X, P->Point.Y, P->Point.Z );
		} );

		if( (PolyFlags & (PF_RenderFog|PF_Translucent|PF_Modulated)) == PF_RenderFog
#ifdef PLATFORM_AMIGA
			// The legacy QuarkTex/StormMesa actor fog overlay saturates model
			// polygons to white or cyan depending on view angle.  BSP fog maps use
			// a separate path and remain enabled.
			&& false
#endif
		)
		{
			ResetTexture( 0 );
			SetBlend( PF_Highlighted );
			DrawRendererFan( NumPts, [&]( INT i )
			{
				FTransTexture* P = Pts[i];
				glColor4f( P->Fog.X, P->Fog.Y, P->Fog.Z, P->Fog.W );
				glVertex3f( P->Point.X, P->Point.Y, P->Point.Z );
			} );
		}

		uunclock(GouraudCycles);
		unguard;
}

void UNOpenGLRenderDevice::DrawTile( FSceneNode* Frame, FTextureInfo& Texture, FLOAT X, FLOAT Y, FLOAT XL, FLOAT YL, FLOAT U, FLOAT V, FLOAT UL, FLOAT VL, FSpanBuffer* Span, FLOAT Z, FPlane Light, FPlane Fog, DWORD PolyFlags )
{
#ifdef NOPENGLDRV_USE_MINIGL
	// 1: clear/swap only, 3: BSP/world only, 4: actors only.
	if( GAmigaMiniGLTestMode == 1 || GAmigaMiniGLTestMode == 3 || GAmigaMiniGLTestMode == 4
		|| GAmigaMiniGLTestMode == 6 || GAmigaMiniGLTestMode == 7 || GAmigaMiniGLTestMode == 8
		|| GAmigaMiniGLTestMode == 9 || GAmigaMiniGLTestMode == 10
		|| GAmigaMiniGLTestMode == 11 || GAmigaMiniGLTestMode == 12
		|| GAmigaMiniGLTestMode == 13 || GAmigaMiniGLTestMode == 14
		|| GAmigaMiniGLTestMode == 15 || GAmigaMiniGLTestMode == 16
		|| GAmigaMiniGLTestMode == 17 || GAmigaMiniGLTestMode == 18 )
		return;
#endif
	guard(UNOpenGLRenderDevice::DrawTile);

	SetSceneNode( Frame );
	uclock(TileCycles);
	const UBOOL IsCanvasOverlay = ( PolyFlags & PF_RenderHint ) != 0;
	// QuarkTex clips Canvas vertices placed exactly on glFrustum's near plane,
	// while Wazp3D accepts them.  Scaling X/Y by the same small Z offset keeps
	// the screen-space position unchanged after perspective division.
	const FLOAT DrawZ = IsCanvasOverlay ? ::Max( Z, 1.01f ) : Z;
#ifdef PLATFORM_AMIGA
	// Single-TMU detail/light/fog passes deliberately change several pieces of
	// global fixed-function state.  Establish a known baseline before every
	// canvas tile instead of relying on CurrentPolyFlags to describe state that
	// a preceding pass may have changed directly.
	glColorMask( GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE );
	glDepthFunc( GL_LEQUAL );
	glDepthMask( GL_TRUE );
	glDisable( GL_BLEND );
	glBlendFunc( GL_ONE, GL_ZERO );
	glDisable( GL_ALPHA_TEST );
	glEnable( GL_TEXTURE_2D );
	glTexEnvi( GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE );
	CurrentPolyFlags = PF_Occlude;
#endif
	if( IsCanvasOverlay )
		glDisable( GL_DEPTH_TEST );
	SetTexture( 0, Texture, ( PolyFlags & PF_Masked ), 0.f );
#ifdef PLATFORM_AMIGA
	// SetTexture may return early on a cache hit; keep texturing enabled even
	// if a previous pass reset the actual GL unit without changing that cache.
	glEnable( GL_TEXTURE_2D );
#endif
	ResetTexture( 1 );
	ResetTexture( 2 );
	ResetTexture( 3 );
	// MiniGL may alter Warp3D state while binding or converting a texture.
	// Program blending last, immediately before submitting the tile.
	SetBlend( PolyFlags );

	if( PolyFlags & PF_Modulated )
		glColor4f( 1.f, 1.f, 1.f, 1.f );
	else
		glColor4f( Light.X, Light.Y, Light.Z, 1.f );

	DrawRendererFan( 4, [&]( INT i )
	{
		switch( i )
		{
		case 0:
			glTexCoord2f( U*TexInfo[0].UMult, V*TexInfo[0].VMult );
			glVertex3f( RFX2*DrawZ*(X-Frame->FX2), RFY2*DrawZ*(Y-Frame->FY2), DrawZ );
			break;
		case 1:
			glTexCoord2f( (U+UL)*TexInfo[0].UMult, V*TexInfo[0].VMult );
			glVertex3f( RFX2*DrawZ*(X+XL-Frame->FX2), RFY2*DrawZ*(Y-Frame->FY2), DrawZ );
			break;
		case 2:
			glTexCoord2f( (U+UL)*TexInfo[0].UMult, (V+VL)*TexInfo[0].VMult );
			glVertex3f( RFX2*DrawZ*(X+XL-Frame->FX2), RFY2*DrawZ*(Y+YL-Frame->FY2), DrawZ );
			break;
		case 3:
			glTexCoord2f( U*TexInfo[0].UMult, (V+VL)*TexInfo[0].VMult );
			glVertex3f( RFX2*DrawZ*(X-Frame->FX2), RFY2*DrawZ*(Y+YL-Frame->FY2), DrawZ );
			break;
		}
	} );

	if( IsCanvasOverlay )
		glEnable( GL_DEPTH_TEST );

	uunclock(TileCycles);
	unguard;
}

void UNOpenGLRenderDevice::Draw2DLine( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FVector P1, FVector P2 )
{

}

void UNOpenGLRenderDevice::Draw2DPoint( FSceneNode* Frame, FPlane Color, DWORD LineFlags, FLOAT X1, FLOAT Y1, FLOAT X2, FLOAT Y2 )
{

}

void UNOpenGLRenderDevice::EndFlash( )
{
	guard(UNOpenGLESRenderDevice::EndFlash);

	if( ColorMod == FPlane( 0.f, 0.f, 0.f, 0.f ) )
		return;

	ResetTexture( 0 );
	ResetTexture( 1 );
	ResetTexture( 2 );
	ResetTexture( 3 );
	SetBlend( PF_Highlighted );

	const FLOAT Z = 1.f;
	const FLOAT RFX2 = RProjZ;
	const FLOAT RFY2 = RProjZ * Aspect;

	glDisable( GL_DEPTH_TEST );

	glColor4fv( &ColorMod.R );
	DrawRendererFan( 4, [&]( INT i )
	{
		glVertex3f( RFX2 * ((i == 1 || i == 2) ? Z : -Z), RFY2 * ((i >= 2) ? Z : -Z), Z );
	} );

	glEnable( GL_DEPTH_TEST );

	unguard;
}

void UNOpenGLRenderDevice::PushHit( const BYTE* Data, INT Count )
{

}

void UNOpenGLRenderDevice::PopHit( INT Count, UBOOL bForce )
{

}

void UNOpenGLRenderDevice::GetStats( char* Result )
{
	guard(UNOpenGLRenderDevice::GetStats)

//	if( Result ) *Result = '\0';
	appSprintf
	(
		Result,
		"OpenGL stats: Bind=%04.1f Image=%04.1f Complex=%04.1f Gouraud=%04.1f Tile=%04.1f",
		GSecondsPerCycle*1000 * BindCycles,
		GSecondsPerCycle*1000 * ImageCycles,
		GSecondsPerCycle*1000 * ComplexCycles,
		GSecondsPerCycle*1000 * GouraudCycles,
		GSecondsPerCycle*1000 * TileCycles
	);

	unguard;
}

void UNOpenGLRenderDevice::ReadPixels( FColor* Pixels )
{
	guard(UNOpenGLRenderDevice::ReadPixels);

	glPixelStorei( GL_UNPACK_ALIGNMENT, 0 );
	glReadPixels( 0, 0, Viewport->SizeX, Viewport->SizeY, GL_RGBA, GL_UNSIGNED_BYTE, (void*)Pixels );

	// Swap RGBA -> BGRA and flip vertically.
	for( INT i=0; i<Viewport->SizeY/2; i++ )
	{
		for( INT j=0; j<Viewport->SizeX; j++ )
		{
			Exchange( Pixels[j+i*Viewport->SizeX].R, Pixels[j+(Viewport->SizeY-1-i)*Viewport->SizeX].B );
			Exchange( Pixels[j+i*Viewport->SizeX].G, Pixels[j+(Viewport->SizeY-1-i)*Viewport->SizeX].G );
			Exchange( Pixels[j+i*Viewport->SizeX].B, Pixels[j+(Viewport->SizeY-1-i)*Viewport->SizeX].R );
		}
	}

	unguard;
}

void UNOpenGLRenderDevice::ClearZ( FSceneNode* Frame )
{
	guard(UNOpenGLRenderDevice::ClearZ);

	SetBlend( PF_Occlude );
	glClear( GL_DEPTH_BUFFER_BIT );

	unguard;
}

void UNOpenGLRenderDevice::SetSceneNode( FSceneNode* Frame )
{
	guard(UNOpenGLRenderDevice::SetSceneNode);

	check(Viewport);

	if( !Frame )
	{
		// invalidate current saved data
		CurrentSceneNode.X = -1;
		CurrentSceneNode.FX = -1.f;
		CurrentSceneNode.SizeX = -1;
		return;
	}

	if( Frame->X != CurrentSceneNode.X || Frame->Y != CurrentSceneNode.Y ||
			Frame->XB != CurrentSceneNode.XB || Frame->YB != CurrentSceneNode.YB ||
			Viewport->SizeX != CurrentSceneNode.SizeX || Viewport->SizeY != CurrentSceneNode.SizeY )
	{
		glViewport( Frame->XB, Viewport->SizeY - Frame->Y - Frame->YB, Frame->X, Frame->Y );
		CurrentSceneNode.X = Frame->X;
		CurrentSceneNode.Y = Frame->Y;
		CurrentSceneNode.XB = Frame->XB;
		CurrentSceneNode.YB = Frame->YB;
		CurrentSceneNode.SizeX = Viewport->SizeX;
		CurrentSceneNode.SizeY = Viewport->SizeY;
	}

	if( Frame->FX != CurrentSceneNode.FX || Frame->FY != CurrentSceneNode.FY ||
			Viewport->Actor->FovAngle != CurrentSceneNode.FovAngle )
	{
		RProjZ = appTan( Viewport->Actor->FovAngle * PI / 360.0 );
		Aspect = Frame->FY / Frame->FX;
		RFX2 = 2.0f * RProjZ / Frame->FX;
		RFY2 = 2.0f * RProjZ * Aspect / Frame->FY;
		glMatrixMode( GL_PROJECTION );
		glLoadIdentity();
		glFrustum( -RProjZ, +RProjZ, -Aspect * RProjZ, +Aspect * RProjZ, 1.0, 32768.0 );
		CurrentSceneNode.FX = Frame->FX;
		CurrentSceneNode.FY = Frame->FY;
		CurrentSceneNode.FovAngle = Viewport->Actor->FovAngle;
	}

	unguard;
}

void UNOpenGLRenderDevice::SetBlend( DWORD PolyFlags, UBOOL InverseOrder )
{
	guard(UNOpenGLRenderDevice::SetBlend);

	// Adjust PolyFlags according to Unreal's precedence rules.
	if( !(PolyFlags & (PF_Translucent|PF_Modulated)) )
		PolyFlags |= PF_Occlude;
	else if( PolyFlags & PF_Translucent )
		PolyFlags &= ~PF_Masked;

	// Detect changes in the blending modes.
	DWORD Xor = CurrentPolyFlags ^ PolyFlags;
	if( Xor & (PF_Translucent|PF_Modulated|PF_Invisible|PF_Occlude|PF_Masked|PF_Highlighted) )
	{
		if( Xor&(PF_Translucent|PF_Modulated|PF_Highlighted) )
		{
			EnableRendererBlend( PolyFlags );
			if( PolyFlags & PF_Translucent )
			{
			#ifdef NOPENGLDRV_USE_MINIGL
				// Classic Warp3D commonly rejects screen blending and MiniGL
				// silently falls back to SRC_ALPHA/ONE_MINUS_SRC_ALPHA. UE1's
				// corona palettes are fully opaque, so that fallback draws their
				// black rectangular background. Additive blending is supported
				// and preserves black as transparent for these light sprites.
				if( (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_CLASSIC) || (GAmigaBlendCompat & 1) )
					glBlendFunc( GL_ONE, GL_ONE );
				else
					glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_COLOR );
			#else
				glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_COLOR );
			#endif
			}
			else if( PolyFlags & PF_Modulated )
			{
#ifdef NOPENGLDRV_USE_MINIGL
				// Same restriction as the PF_Translucent case above: the classic
				// backend has no GL_SRC_COLOR destination factor, so UE1's 2x
				// overbright modulation silently does nothing and the surface is
				// left showing whatever the pass underneath put there.  Plain
				// one-times modulation is the supported equivalent.
				if( (MiniGLDispatch->backendFlags & MINIGL_BACKEND_FLAG_CLASSIC) || (GAmigaBlendCompat & 2) )
					glBlendFunc( GL_DST_COLOR, GL_ZERO );
				else
					glBlendFunc( GL_DST_COLOR, GL_SRC_COLOR );
#else
				glBlendFunc( GL_DST_COLOR, GL_SRC_COLOR );
#endif
			}
			else if( PolyFlags & PF_Highlighted )
			{
				glBlendFunc( GL_ONE, GL_ONE_MINUS_SRC_ALPHA );
			}
			else
			{
				glDisable( GL_BLEND );
				glBlendFunc( GL_ONE, GL_ZERO );
			}
		}
		if( Xor & PF_Invisible )
		{
			UBOOL Show = !( PolyFlags & PF_Invisible );
			glColorMask( Show, Show, Show, Show );
		}
		if( Xor & PF_Occlude )
		{
			glDepthMask( (PolyFlags & PF_Occlude) != 0 );
		}
		if( Xor & PF_Masked )
		{
			if( PolyFlags & PF_Masked )
				glEnable( GL_ALPHA_TEST );
			else
				glDisable( GL_ALPHA_TEST );
		}
	}

	CurrentPolyFlags = PolyFlags;

	unguard;
}

void UNOpenGLRenderDevice::ResetTexture( INT TMU )
{
	guard(UNOpenGLRenderDevice::ResetTexture);

	// With a single texture unit, TMU 1..3 are bookkeeping slots only.  Acting
	// on them would disable GL_TEXTURE_2D on the actually active unit zero while
	// leaving unit zero's cache entry intact, so later SetTexture(0) would skip
	// the re-enable and all following geometry would be drawn untextured.
	if( !UseMultiTexture && TMU != 0 )
	{
		TexInfo[TMU].CurrentCacheID = 0;
		return;
	}

	if( TexInfo[TMU].CurrentCacheID != 0 )
	{
		uclock(BindCycles);
		if( UseMultiTexture )
			glActiveTexture( GL_TEXTURE0 + TMU );
		glBindTexture( GL_TEXTURE_2D, 0 );
		glDisable( GL_TEXTURE_2D );
		TexInfo[TMU].CurrentCacheID = 0;
		uunclock(BindCycles);
	}

	unguard;
}

void UNOpenGLRenderDevice::SetTexture( INT TMU, FTextureInfo& Info, DWORD PolyFlags, FLOAT PanBias )
{
	guard(UNOpenGLRenderDevice::SetTexture);

	// Set panning.
	FTexInfo& Tex = TexInfo[TMU];
	Tex.UPan      = Info.Pan.X + PanBias*Info.UScale;
	Tex.VPan      = Info.Pan.Y + PanBias*Info.VScale;

	// Account for all the impact on scale normalization.
	Tex.UMult = 1.f / (Info.UScale * static_cast<FLOAT>(Info.USize));
	Tex.VMult = 1.f / (Info.VScale * static_cast<FLOAT>(Info.VSize));

	// Find in cache.
	QWORD NewCacheID = Info.CacheID;
	if( ( PolyFlags & PF_Masked ) && Info.Palette )
		NewCacheID |= MASKED_TEXTURE_TAG;
	UBOOL RealtimeChanged = ( Info.TextureFlags & TF_RealtimeChanged );
#ifdef NOPENGLDRV_USE_MINIGL
	// Mode 12 uploads the first real UE1 base texture, then deliberately uses
	// that single MiniGL texture object for every BSP surface.
	if( GAmigaMiniGLTestMode == 12 )
	{
		if( GAmigaLockedTextureCacheID == 0 )
			GAmigaLockedTextureCacheID = NewCacheID;
		else
			NewCacheID = GAmigaLockedTextureCacheID;
		RealtimeChanged = false;
	}

	// Mode 5 keeps normal rendering but suppresses repeated uploads of already
	// cached realtime textures.  Initial uploads still happen normally.
	if( GAmigaMiniGLTestMode == 5 && NewCacheID == Tex.CurrentCacheID )
		RealtimeChanged = false;
#endif
	if( NewCacheID == Tex.CurrentCacheID && !RealtimeChanged )
	{
#ifdef NOPENGLDRV_USE_MINIGL
		// Mode 9 checks whether PiStorm3D is being overwhelmed or corrupted by
		// redundant bind/filter state on every BSP surface cache hit.
		if( GAmigaMiniGLTestMode == 9 || GAmigaMiniGLTestMode == 11
			|| GAmigaMiniGLTestMode == 12 || GAmigaMiniGLTestMode == 13
			|| GAmigaMiniGLTestMode == 16 || GAmigaMiniGLTestMode == 17
			|| GAmigaMiniGLTestMode == 18 )
			return;
		// MiniGL's global state may be changed by another pass even though UE1's
		// texture cache still points at the same object.  Rebind and restore the
		// requested filter on cache hits instead of silently inheriting NEAREST.
		if( UseMultiTexture )
			glActiveTexture( GL_TEXTURE0 + TMU );
		glEnable( GL_TEXTURE_2D );
		FCachedTexture* CachedBind = BindMap.Find( NewCacheID );
		if( CachedBind )
		{
			glBindTexture( GL_TEXTURE_2D, CachedBind->Id );
			const UBOOL UseNearest = WantsNearest(Info,PolyFlags);
			SetCachedTextureFilter( CachedBind, UseNearest ? GL_NEAREST : GL_LINEAR, UseNearest ? GL_NEAREST : GL_LINEAR );
		}
#endif
		return;
	}

	// Make current.
	uclock(BindCycles);
	Tex.CurrentCacheID = NewCacheID;
	FCachedTexture* Bind = BindMap.Find( NewCacheID );
	FCachedTexture* OldBind = Bind;
	if( !Bind )
	{
		// New texture.
		Bind = BindMap.Add( NewCacheID, FCachedTexture() );
		glGenTextures( 1, &Bind->Id );
		Bind->AppliedMinFilter = Bind->AppliedMagFilter = 0;
		TexAlloc.AddItem( Bind->Id );
	}

	if( UseMultiTexture )
		glActiveTexture( GL_TEXTURE0 + TMU );
	glEnable( GL_TEXTURE_2D );
	glBindTexture( GL_TEXTURE_2D, Bind->Id );
	uunclock(BindCycles);

	if( !OldBind || RealtimeChanged )
	{
		// New texture or it has changed, upload it.
		Info.TextureFlags &= ~TF_RealtimeChanged;
#ifdef NOPENGLDRV_USE_MINIGL
		// MiniGL test 13 programs a complete, non-mipmapped sampling state before
		// glTexImage2D.  Some restricted backends inspect the default mipmapped
		// minification filter while allocating the texture and never recover.
		if( GAmigaMiniGLTestMode == 13 )
		{
			const UBOOL UseNearestBeforeUpload = WantsNearest(Info,PolyFlags);
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, UseNearestBeforeUpload ? GL_NEAREST : GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, UseNearestBeforeUpload ? GL_NEAREST : GL_LINEAR );
		}
#endif
		// Upload/reallocation may reset driver-side sampling state.
		Bind->AppliedMinFilter = Bind->AppliedMagFilter = 0;
		UploadTexture( Info, ( PolyFlags & PF_Masked ), !OldBind );
#ifdef NOPENGLDRV_USE_MINIGL
		// Test whether the driver retains or asynchronously converts the shared
		// Compose upload buffer after glTexImage2D has returned.
		if( GAmigaMiniGLTestMode == 11 )
			glFinish();
#endif
		// QuarkTex/AmigaMesa treats UE1 mip chains as incomplete and samples the
		// whole texture white.  Keep the verified level-zero, non-mipmapped path.
#ifdef PLATFORM_AMIGA
		const UBOOL HasMipmaps = false;
#else
		const UBOOL HasMipmaps = Info.NumMips > 1;
#endif
		if( WantsNearest(Info,PolyFlags) )
		{
			SetCachedTextureFilter( Bind, HasMipmaps ? GL_NEAREST_MIPMAP_NEAREST : GL_NEAREST, GL_NEAREST );
		}
		else
		{
			SetCachedTextureFilter( Bind, HasMipmaps ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR, GL_LINEAR );
		}
#ifdef PLATFORM_AMIGA
		if( GAmigaTextureTraceRemaining > 0 )
		{
			const GLenum Error = glGetError();
			AmigaDebugLogf( "[Amiga] AUTO GL texture params: cache=%08lx%08lx id=%lu mips=%d minf=%d error=0x%04lx",
				(unsigned long)(Info.CacheID >> 32), (unsigned long)(Info.CacheID & 0xffffffffu),
				(unsigned long)Bind->Id, Info.NumMips,
				(int)(WantsNearest(Info,PolyFlags) ? 0 : 1),
				(unsigned long)Error );
		}
#endif
	}
#ifdef NOPENGLDRV_USE_MINIGL
	else
	{
		// A cached texture bound after a different texture needs the current
		// option too, even when neither texture data nor cache ID changed.
		const GLenum Filter=WantsNearest(Info,PolyFlags) ? GL_NEAREST : GL_LINEAR;
		SetCachedTextureFilter(Bind,Filter,Filter);
	}
#endif

	unguard;
}

UBOOL UNOpenGLRenderDevice::WantsNearest(const FTextureInfo& Info,DWORD PolyFlags) const
{
#ifdef NOPENGLDRV_USE_MINIGL
	return (PolyFlags & PF_NoSmooth) || TextureFilter==0;
#else
	return (PolyFlags & PF_NoSmooth) || (NoFiltering && Info.Palette);
#endif
}

void UNOpenGLRenderDevice::SetCachedTextureFilter( FCachedTexture* Bind, GLenum Min, GLenum Mag )
{
#ifdef NOPENGLDRV_USE_MINIGL
	if( ((GAmigaPerfMask & 2) || GAmigaFilterCache == 1)
		&& Bind->AppliedMinFilter == Min && Bind->AppliedMagFilter == Mag )
		return;
#endif
	// Set the pair together: Classic keeps MinFilter/MagFilter in its context
	// as well as in texture objects, so changing only one after a bind is unsafe.
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, Min );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, Mag );
	Bind->AppliedMinFilter = Min;
	Bind->AppliedMagFilter = Mag;
}

void UNOpenGLRenderDevice::EnsureComposeSize( const DWORD NewSize )
{
	if( NewSize > ComposeSize )
	{
		Compose = (BYTE*)appRealloc( Compose, NewSize, "GLComposeBuf" );
	}
	verify( Compose );
}

void UNOpenGLRenderDevice::ConvertTextureMipI8( const FMipmap* Mip, const FColor* Palette, const UBOOL Masked, BYTE*& UploadBuf, GLenum& UploadFormat, GLenum& InternalFormat )
{
	// 8-bit indexed. We have to fix the alpha component since it's mostly garbage.
	DWORD i;
	if( UseHwPalette )
	{
		// GL has support for palettized textures, use it. Still have to fix the alpha.
		const DWORD* SrcPal = (const DWORD*)Palette;
		EnsureComposeSize( 256 * 4 );
		DWORD* DstPal = (DWORD*)Compose;
		UploadBuf = Mip->DataPtr;
		InternalFormat = GL_COLOR_INDEX8_EXT;
		UploadFormat = GL_COLOR_INDEX8_EXT;
		i = 0;
		// index 0 is transparent in masked textures
		if( Masked )
		{
			*DstPal++ = 0;
			++i;
		}
		// 255 alpha on the rest of the palette
		for( ; i < 256; ++i )
			*DstPal++ = *SrcPal++ | ALPHA_MASK;
		// set palette pointer
		glColorTableEXT( GL_TEXTURE_2D, GL_RGBA8, 256, GL_RGBA, GL_UNSIGNED_BYTE, (void*)Compose );
	}
	else
	{
		// No support for palettized textures. Expand to RGBA8888 and fix alpha.
		const BYTE* Src = (const BYTE*)Mip->DataPtr;
		const DWORD* Pal = (const DWORD*)Palette;
		const DWORD Count = Mip->USize * Mip->VSize;
		EnsureComposeSize( Count * 4 );
		DWORD* Dst = (DWORD*)Compose;
		UploadBuf = Compose;
		UploadFormat = GL_RGBA;
#ifdef NOPENGLDRV_USE_MINIGL
		// MiniGL v10 accepts GL_RGBA here; GL_RGBA8 produces white textures.
		InternalFormat = GL_RGBA;
#else
		InternalFormat = GL_RGBA8;
#endif
		if( Masked )
		{
			// index 0 is transparent
#if __INTEL_BYTE_ORDER__
			for( i = 0; i < Count; ++i, ++Src )
				*Dst++ = *Src ? ( Pal[*Src] | ALPHA_MASK ) : 0;
#else
			for( i = 0; i < Count; ++i, ++Src )
			{
				FColor Color = Palette[*Src];
				Color.A = *Src ? 255 : 0;
				*Dst++ = (Color.R << 24) | (Color.G << 16) | (Color.B << 8) | Color.A;
			}
#endif
		}
		else
		{
			// index 0 is whatever
#if __INTEL_BYTE_ORDER__
			for( i = 0; i < Count; ++i )
				*Dst++ = ( Pal[*Src++] | ALPHA_MASK );
#else
			for( i = 0; i < Count; ++i, ++Src )
			{
				FColor Color = Palette[*Src];
				Color.A = 255;
				*Dst++ = (Color.R << 24) | (Color.G << 16) | (Color.B << 8) | Color.A;
			}
#endif
		}
#ifdef PLATFORM_AMIGA
		// Diagnostic only: is the expanded I8 texture actually non-white?
		// Runs in the upload path (not per-frame drawing), a bounded number of
		// times, so it cannot disturb the renderer's hot loop.
		if( GAmigaI8TraceRemaining > 0 )
		{
			const BYTE* Out = (const BYTE*)Compose;
			BYTE OutMin[4]={255,255,255,255}, OutMax[4]={0,0,0,0};
			for( DWORD k = 0; k < Count; ++k )
				for( INT c = 0; c < 4; ++c )
				{
					OutMin[c]=Min(OutMin[c],Out[k*4+c]);
					OutMax[c]=Max(OutMax[c],Out[k*4+c]);
				}
			AmigaDebugLogf( "[Amiga] AUTO I8 tex %lux%lu masked=%d min=%02x%02x%02x%02x max=%02x%02x%02x%02x pal0=%02x%02x%02x",
				(unsigned long)Mip->USize,(unsigned long)Mip->VSize,(int)Masked,
				OutMin[0],OutMin[1],OutMin[2],OutMin[3],
				OutMax[0],OutMax[1],OutMax[2],OutMax[3],
				Palette[1].R,Palette[1].G,Palette[1].B );
			--GAmigaI8TraceRemaining;
		}
#endif
	}
}

void UNOpenGLRenderDevice::ConvertTextureMipBGRA7777( const FMipmap* Mip, BYTE*& UploadBuf, GLenum& UploadFormat, GLenum& InternalFormat )
{
	// BGRA8888. This is actually a BGRA7777 lightmap, so we need to scale it.
	const BYTE* Src = (const BYTE*)Mip->DataPtr;
	const BYTE* RawStart = Src;
	const DWORD Count = Mip->USize * Mip->VSize;
	EnsureComposeSize( Count * 4 );
	BYTE* Dst = (BYTE*)Compose;
	UploadBuf = Compose;
#ifdef NOPENGLDRV_USE_MINIGL
	InternalFormat = GL_RGBA;
#else
	InternalFormat = GL_RGBA8;
#endif
	if( UseBGRA )
	{
		UploadFormat = GL_BGRA;
		for( DWORD i = 0; i < Count; ++i )
		{
			*Dst++ = (*Src++) << 1;
			*Dst++ = (*Src++) << 1;
			*Dst++ = (*Src++) << 1;
			*Dst++ = (*Src++) << 1;
		}
	}
	else
	{
		// Swap BGRA -> RGBA
		UploadFormat = GL_RGBA;
		for( DWORD i = 0; i < Count; ++i, Src += 4 )
		{
			*Dst++ = Src[2] << 1;
			*Dst++ = Src[1] << 1;
			*Dst++ = Src[0] << 1;
			*Dst++ = Src[3] << 1;
		}
	}
#ifdef PLATFORM_AMIGA
	if( TextureUploadSemantic==1 )
	{
		// UE1 normally gets its 2x overbright term from (DST_COLOR, SRC_COLOR).
		// The Amiga path uses the widely supported (DST_COLOR, ZERO), therefore
		// apply that second factor here after expanding BGRA7777 to eight bits.
		for( DWORD i=0; i<Count; ++i )
		{
			UploadBuf[i*4+0]=Min((INT)UploadBuf[i*4+0]*2,255);
			UploadBuf[i*4+1]=Min((INT)UploadBuf[i*4+1]*2,255);
			UploadBuf[i*4+2]=Min((INT)UploadBuf[i*4+2]*2,255);
			UploadBuf[i*4+3]=255;
		}
	}
	if( GAmigaLightmapTraceRemaining > 0 )
	{
		BYTE RawMax[4]={0,0,0,0};
		BYTE OutMax[4]={0,0,0,0};
		for( DWORD i=0; i<Count; ++i )
		{
			for( INT c=0; c<4; ++c )
			{
				RawMax[c]=Max(RawMax[c],RawStart[i*4+c]);
				OutMax[c]=Max(OutMax[c],UploadBuf[i*4+c]);
			}
		}
		AmigaDebugLogf( "[Amiga] BGRA7 data kind=%d %lux%lu raw0=%02x%02x%02x%02x rawmax=%02x%02x%02x%02x out0=%02x%02x%02x%02x outmax=%02x%02x%02x%02x",
			TextureUploadSemantic,(unsigned long)Mip->USize,(unsigned long)Mip->VSize,
			RawStart[0],RawStart[1],RawStart[2],RawStart[3],RawMax[0],RawMax[1],RawMax[2],RawMax[3],
			UploadBuf[0],UploadBuf[1],UploadBuf[2],UploadBuf[3],OutMax[0],OutMax[1],OutMax[2],OutMax[3] );
		--GAmigaLightmapTraceRemaining;
	}
#endif
}

void UNOpenGLRenderDevice::UploadTexture( FTextureInfo& Info, UBOOL Masked, UBOOL NewTexture )
{
	guard(UNOpenGLRenderDevice::UploadTexture);

	if( !Info.Mips[0] )
	{
		debugf( NAME_Warning, "Encountered texture with invalid mips!" );
		return;
	}

	// Upload level zero on Amiga; uploading the UE1 chains makes QuarkTex mark
	// some textures incomplete and return white for every sample.
	uclock(ImageCycles);
#ifdef PLATFORM_AMIGA
	const INT UploadMipCount = Min( Info.NumMips, 1 );
#else
	const INT UploadMipCount = Info.NumMips;
#endif
	for( INT MipIndex = 0; MipIndex < UploadMipCount; ++MipIndex )
	{
		const FMipmap* Mip = Info.Mips[MipIndex];
		BYTE* UploadBuf;
		GLenum UploadFormat;
		GLenum InternalFormat;
		if( !Mip || !Mip->DataPtr )
			break;
		// Convert texture if needed.
#ifdef NOPENGLDRV_USE_MINIGL
		if( GAmigaMiniGLTestMode == 18 )
		{
			// Exercise UE1's normal texture cache, object creation, binding and UVs,
			// but remove UE texture conversion and contents from the equation.
			static BYTE KnownChecker[16] =
			{
				255, 255, 255, 255,   32,  32,  32, 255,
				 32,  32,  32, 255,  255, 255, 255, 255
			};
			UploadBuf = KnownChecker;
			UploadFormat = GL_RGBA;
			InternalFormat = GL_RGBA;
		}
		else
#endif
		if( Info.Palette )
			ConvertTextureMipI8( Mip, Info.Palette, Masked, UploadBuf, UploadFormat, InternalFormat );
		else
			ConvertTextureMipBGRA7777( Mip, UploadBuf, UploadFormat, InternalFormat );
		INT UploadUSize = Mip->USize;
		INT UploadVSize = Mip->VSize;
#ifdef NOPENGLDRV_USE_MINIGL
		// Mode 16 retains UE1's real conversion path and texture-object churn, but
		// reduces every upload to four representative RGBA pixels.
		if( GAmigaMiniGLTestMode == 16 )
		{
			const INT SourceX[2] = { 0, Max( Mip->USize - 1, 0 ) };
			const INT SourceY[2] = { 0, Max( Mip->VSize - 1, 0 ) };
			for( INT y = 0; y < 2; ++y )
				for( INT x = 0; x < 2; ++x )
				{
					const BYTE* S = UploadBuf + ( SourceY[y] * Mip->USize + SourceX[x] ) * 4;
					BYTE* D = GAmigaTinyTextureUpload + ( y * 2 + x ) * 4;
					D[0] = S[0]; D[1] = S[1]; D[2] = S[2]; D[3] = S[3];
				}
			UploadBuf = GAmigaTinyTextureUpload;
			UploadUSize = UploadVSize = 2;
		}
		else if( GAmigaMiniGLTestMode == 18 )
		{
			UploadUSize = UploadVSize = 2;
		}
#endif
		// Upload to GL. New storage uses TexImage; realtime/procedural updates
		// retain the original TexSubImage path.
#if defined(PLATFORM_AMIGA) && defined(NOPENGLDRV_USE_MINIGL)
		if( GAmigaMiniGLTraceFrames > 0 )
			AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=texture-upload-before new=%d mip=%d size=%dx%d",
				(unsigned long)GAmigaFrameSerial, (INT)NewTexture, MipIndex, Mip->USize, Mip->VSize );
#endif
		if( NewTexture )
		{
#ifdef NOPENGLDRV_USE_MINIGL
			if( GAmigaMiniGLTestMode == 17 )
			{
				glTexImage2D( GL_TEXTURE_2D, MipIndex, InternalFormat, UploadUSize, UploadVSize, 0, UploadFormat, GL_UNSIGNED_BYTE, NULL );
				glTexSubImage2D( GL_TEXTURE_2D, MipIndex, 0, 0, UploadUSize, UploadVSize, UploadFormat, GL_UNSIGNED_BYTE, (void*)UploadBuf );
			}
			else
#endif
				glTexImage2D( GL_TEXTURE_2D, MipIndex, InternalFormat, UploadUSize, UploadVSize, 0, UploadFormat, GL_UNSIGNED_BYTE, (void*)UploadBuf );
		}
		else
			glTexSubImage2D( GL_TEXTURE_2D, MipIndex, 0, 0, UploadUSize, UploadVSize, UploadFormat, GL_UNSIGNED_BYTE, (void*)UploadBuf );
#if defined(PLATFORM_AMIGA) && defined(NOPENGLDRV_USE_MINIGL)
		if( GAmigaMiniGLTraceFrames > 0 )
			AmigaDebugLogf( "[Amiga] AUTO MGL frame=%lu phase=texture-upload-after new=%d mip=%d size=%dx%d",
				(unsigned long)GAmigaFrameSerial, (INT)NewTexture, MipIndex, Mip->USize, Mip->VSize );
#endif
#ifdef PLATFORM_AMIGA
		if( GAmigaTextureTraceRemaining > 0 )
		{
			const GLenum Error = glGetError();
			AmigaDebugLogf( "[Amiga] AUTO GL texture upload: %dx%d pal=%d masked=%d rgba=%02x%02x%02x%02x error=0x%04lx",
				Mip->USize, Mip->VSize, Info.Palette != NULL, Masked,
				UploadBuf[0], UploadBuf[1], UploadBuf[2], UploadBuf[3], (unsigned long)Error );
			--GAmigaTextureTraceRemaining;
		}
#endif
	}
	uunclock(ImageCycles);

	unguard;
}

void UNOpenGLRenderDevice::UpdateSwapInterval()
{
	guard(UNOpenGLRenderDevice::UpdateSwapInterval);

#ifdef NOPENGLDRV_USE_MINIGL
	mglEnableSync( SwapInterval != 0 ? GL_TRUE : GL_FALSE );
#else
	if( SwapInterval < -1 )
	{
		SwapInterval = -1;
	}

	if( SDL_GL_SetSwapInterval( SwapInterval ) < 0 )
	{
		debugf( NAME_Warning, "Failed to set swap interval %d: %s", SwapInterval, SDL_GetError() );
		if( SwapInterval < 0 )
		{
			// Adaptive VSync not supported, try normal VSync.
			SwapInterval = 1;
			UpdateSwapInterval();
		}
	}
#endif

	unguard;
}
