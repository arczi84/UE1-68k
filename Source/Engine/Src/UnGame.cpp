/*=============================================================================
	UnGame.cpp: Unreal game engine.
	Copyright 1997 Epic MegaGames, Inc. This software is a trade secret.

	Revision history:
		* Created by Tim Sweeney
=============================================================================*/

#include "EnginePrivate.h"
#include "UnRender.h"
#include "UnNet.h"

#ifdef UE_AMIGA_GPROF
extern "C" void _moncleanup(void);
#endif

#ifdef PLATFORM_AMIGA
extern "C" void AmigaDebugLogf( const char* Fmt, ... );
extern "C" DWORD SDL_GetTicks( void );
extern INT GAmigaActorProfileRequests;
extern INT GAmigaStartupTraceFrames;
#endif

// Unattended benchmark state.  Kept at file scope rather than in UGameEngine
// because that class is CLASS_Config and its size is asserted against the
// script-side class layout in Init().
static UBOOL AutoTimedemoActive = 0;
// Set once the completed-cycle result has been seen; Tick() then quits.  The
// exit cannot happen inside the script call itself, which is still running.
static UBOOL AutoTimedemoDone   = 0;
static char  AutoTimedemoResult[256] = "";
// Wall-clock accounting, kept separate from the HUD's figures.  The script
// derives its numbers from Level.TimeSeconds, but the Amiga main loop clamps
// the simulation step to 100 ms (see SDLLaunch.cpp) so that a disk or audio
// stall cannot explode the physics.  Game time therefore runs slower than real
// time whenever a frame takes longer than that, which silently inflates the
// reported framerate and puts a hard floor of 10 FPS under the minimum.
// These counters use appSeconds(), so stalls are reflected honestly.
static INT    AutoTimedemoWallFrames = 0;
static DOUBLE AutoTimedemoWallStart  = 0.0;
static DOUBLE AutoTimedemoWallLast   = 0.0;
static DOUBLE AutoTimedemoWorstFrame = 0.0;
// Safety net: if the flyby never completes (broken path, stuck camera), give
// up after this many seconds rather than running forever.
static INT   AutoTimedemoTimeout = 300;
static DOUBLE AutoTimedemoStart  = 0.0;
// Identity only: never dereference this pointer after the script removes its HUD.
static UObject* AutoTimedemoHUD = NULL;

static void CancelAutoTimedemo( const char* Reason )
{
	if( !AutoTimedemoActive )
		return;
	AutoTimedemoActive = 0;
	AutoTimedemoDone = 0;
	AutoTimedemoHUD = NULL;
	AutoTimedemoResult[0] = 0;
	AutoTimedemoStart = 0.0;
	AutoTimedemoWallFrames = 0;
	AutoTimedemoWallStart = AutoTimedemoWallLast = AutoTimedemoWorstFrame = 0.0;
	debugf( "AutoTimedemo: cancelled (%s); automatic exit disabled", Reason );
}

//
// Watch broadcast messages for the TimeDemo HUD's completed-cycle result.
// Called from APlayerPawn::execClientMessage.
//
void AutoTimedemoNotifyMessage( const char* Msg )
{
	if( !AutoTimedemoActive || AutoTimedemoDone || !Msg )
		return;
	// TimeDemoIntroNullHud::FinishCycle builds "Result: <n> FPS (<n> frames, <n> seconds)".
	if( appStrncmp( Msg, "Result:", 7 )!=0 )
		return;
	appStrncpy( AutoTimedemoResult, Msg, ARRAY_COUNT(AutoTimedemoResult) );
	AutoTimedemoResult[ARRAY_COUNT(AutoTimedemoResult)-1] = 0;
	AutoTimedemoDone = 1;
}

/*-----------------------------------------------------------------------------
	Object class implementation.
-----------------------------------------------------------------------------*/

IMPLEMENT_CLASS(UGameEngine);

/*-----------------------------------------------------------------------------
	Temporary.
-----------------------------------------------------------------------------*/

void UGameEngine::PaintProgress()
{
	guard(PaintProgress);

	FVector LoadFog(0,.1,.25);
	FVector LoadScale(.2,.2,.2);
	UViewport* Viewport=Client->Viewports(0);
	Exchange(Viewport->Actor->FlashFog,LoadFog);
	Exchange(Viewport->Actor->FlashScale,LoadScale);
	Draw( Viewport, NULL, NULL );
	Exchange(Viewport->Actor->FlashFog,LoadFog);
	Exchange(Viewport->Actor->FlashScale,LoadScale);

	unguard;
}

INT UGameEngine::ChallengeResponse( INT Challenge )
{
	guard(UGameEngine::ChallengeResponse);
	return (Challenge*237) ^ (0x93fe92Ce) ^ (Challenge>>16) ^ (Challenge<<16);
	unguard;
}

/*-----------------------------------------------------------------------------
	Game init and exit.
-----------------------------------------------------------------------------*/

//
// Construct the game engine.
//
UGameEngine::UGameEngine()
: LastURL("")
{}

//
// Class creator.
//
void UGameEngine::InternalClassInitializer( UClass* Class )
{
	guard(UGameEngine::InternalClassInitializer);
	if( appStricmp(Class->GetName(),"GameEngine")==0 )
	{
		(new(Class,"ServerActors",  RF_Public)UStringProperty( CPP_PROPERTY(ServerActors  ), "Settings", CPF_Config, 96 ))->ArrayDim=16;
		(new(Class,"ServerPackages",RF_Public)UStringProperty( CPP_PROPERTY(ServerPackages), "Settings", CPF_Config, 96 ))->ArrayDim=16;
	}
	unguard;
}

//
// Initialize the game engine.
//
void UGameEngine::Init()
{
	guard(UGameEngine::Init);
	check(sizeof(*this)==GetClass()->GetPropertiesSize());

	// Call base.
	UEngine::Init();

	// Init variables.
	GLevel = NULL;

	// Delete temporary files in cache.
	appCleanFileCache();

	// If not a dedicated server.
	if( GIsClient )
	{	
		// Init client.
		UClass* ClientClass = GObj.LoadClass( UClient::StaticClass, NULL, "ini:Engine.Engine.ViewportManager", NULL, LOAD_NoFail | LOAD_KeepImports, NULL );
		Client = ConstructClassObject<UClient>( ClientClass );
		Client->Init( this );

		// Init rendering.
		UClass* RenderClass = GObj.LoadClass( URenderBase::StaticClass, NULL, "ini:Engine.Engine.Render", NULL, LOAD_NoFail | LOAD_KeepImports, NULL );
		Render = ConstructClassObject<URenderBase>( RenderClass );
		Render->Init( this );
	}

	// Load the entry level.
	char Error256[256];
	if( Client )
	{
		if( !LoadMap( FURL("Entry"), NULL, Error256 ) )
			appErrorf( LocalizeError("LoadEntry"), Error256 );
		Exchange( GLevel, GEntry );
	}

	// Create default URL.
	FURL DefaultURL;
	DefaultURL.GetConfigOptions( "DefaultPlayer" );

	// Enter initial world.
	char AutoURL[4096]="";
	const char* Tmp = appCmdLine();
	if
	(	!ParseToken( Tmp, AutoURL, ARRAY_COUNT(AutoURL), 0 )
	||	AutoURL[0]=='-' )
		appStrcpy( AutoURL, *FURL::DefaultLocalMap );
	FURL URL( &DefaultURL, AutoURL, TRAVEL_Partial );
	if( !URL.Valid )
		appErrorf( LocalizeError("InvalidUrl"), AutoURL );
	UBOOL Success = Browse( FURL(&LastURL,AutoURL,TRAVEL_Partial), Error256 );

	// If waiting for a network connection, go into the starting level.
	if( !Success && !Error256[0] && appStricmp( AutoURL, *FURL::DefaultLocalMap )!=0 )
		Success = Browse( FURL(&LastURL,*FURL::DefaultLocalMap,TRAVEL_Partial), Error256 );

	// Handle failure.
	if( !Success )
		appErrorf( LocalizeError("FailedBrowse"), AutoURL, Error256 );

	// Open initial Viewport.
	if( Client )
	{
		UViewport* Viewport = Client->NewViewport( GLevel, NAME_None );
		char Error256[256];
		if( !GLevel->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, "", Error256 ) )
			appErrorf( Error256 );
		Viewport->Input->Init( Viewport, GSystem );
		Viewport->OpenWindow( NULL, 0, Client->ViewportX, Client->ViewportY, INDEX_NONE, INDEX_NONE );
		if( Audio )
			Audio->SetViewport( Viewport );
		if( GPendingLevel )
		{
			// Reprint connecting message.
			char Msg1[256], Msg2[256];
			appSprintf( Msg1, "Connecting (F10 Cancels):" );
			appSprintf( Msg2, "unreal://%s/%s", *URL.Host, *URL.Map );
			SetProgress( Msg1, Msg2, 60.0 );
		}
	}
	// Unattended benchmarking: [Engine.GameEngine] AutoTimedemo=1 in the ini
	// runs the flyby immediately, writes the result to a file and quits, so a
	// framerate can be measured without touching the console.
	UBOOL AutoTimedemo = 0;
	GetConfigBool( "Engine.GameEngine", "AutoTimedemo", AutoTimedemo );
	if( AutoTimedemo && Client && Client->Viewports.Num() )
	{
		GetConfigInt( "Engine.GameEngine", "AutoTimedemoTimeout", AutoTimedemoTimeout );
		if( AutoTimedemoTimeout <= 0 )
			AutoTimedemoTimeout = 300;
		debugf( NAME_Init, "AutoTimedemo: running one flyby cycle (timeout %i s)",
			AutoTimedemoTimeout );
		Exec( "TIMEDEMO", GSystem );
		AutoTimedemoActive = 1;
		AutoTimedemoDone   = 0;
		AutoTimedemoStart  = 0.0;
		AutoTimedemoResult[0] = 0;
		AutoTimedemoWallFrames = 0;
		AutoTimedemoWallStart = AutoTimedemoWallLast = AutoTimedemoWorstFrame = 0.0;
		AutoTimedemoHUD = Client->Viewports(0)->Actor
			? Client->Viewports(0)->Actor->myHUD : NULL;
	}

	debugf( NAME_Init, "Game engine initialized" );
	unguard;
}

//
// Game exit.
//
void UGameEngine::Destroy()
{
	guard(UGameEngine::Destroy);

	// Game exit.
	if( GPendingLevel )
		CancelPending();
	GLevel = NULL;
	debugf( NAME_Exit, "Game engine shut down" );

	UEngine::Destroy();
	unguard;
}

//
// Progress text.
//
void UGameEngine::SetProgress( const char* Str1, const char* Str2, FLOAT Seconds )
{
	guard(UGameEngine::SetProgress);
	if( Client && Client->Viewports.Num() )
	{
		APlayerPawn* Actor = Client->Viewports(0)->Actor;
		if( Seconds==-1.0 )
		{
			// Upgrade message.
			Actor->eventShowUpgradeMenu();
		}
		appStrncpy( Actor->ProgressMessage, Str1, ARRAY_COUNT(Actor->ProgressMessage) );
		appStrncpy( Actor->ProgressMessageTwo, Str2, ARRAY_COUNT(Actor->ProgressMessageTwo) );
		Actor->ProgressTimeOut = Actor->Level->TimeSeconds + Seconds;
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Command line executor.
-----------------------------------------------------------------------------*/

//
// This always going to be the last exec handler in the chain. It
// handles passing the command to all other global handlers.
//
UBOOL UGameEngine::Exec( const char* Cmd, FOutputDevice* Out )
{
	guard(UGameEngine::Exec);
	const char *Str = Cmd;
	if( ParseCommand( &Str, "TIMEDEMO" ) )
	{
		char IntroMap[256] = "Unreal.unr";
		GetConfigString( "URL", "LocalMap", IntroMap, ARRAY_COUNT(IntroMap) );
		char Error256[256] = "";
		if( !Browse( FURL(&LastURL,IntroMap,TRAVEL_Partial), Error256 ) )
		{
			if( Error256[0] )
				Out->Logf( "Timedemo intro load failed: %s", Error256 );
			return 1;
		}

		if( Client && Client->Viewports.Num() && Client->Viewports(0)->Actor )
			Client->Viewports(0)->Actor->ScriptConsoleExec( "SUMMON TIMEDEMO.TIMEDEMO", Out );
		else
			Out->Log( "No active viewport for timedemo" );
		return 1;
	}
	else if( ParseCommand( &Str, "OPEN" ) )
	{
		char Error256[256];
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Partial), Error256 ) && Error256[0] )
			Out->Logf( "Open failed: %s", Error256 );
		return 1;
	}
	else if( ParseCommand( &Str, "START" ) )
	{
		char Error256[256];
		if( !Browse( FURL(&LastURL,Str,TRAVEL_Absolute), Error256 ) && Error256[0] )
			Out->Logf( "Start failed: %s", Error256 );
		return 1;
	}
	else if( ParseCommand(&Str,"SAVEGAME") )
	{
		if( !GIsEditor && appIsDigit(Str[0]) && Str[1]==0 )
			SaveGame( appAtoi(Str) );
		return 1;
	}
	else if( ParseCommand( &Cmd, "CANCEL" ) )
	{
		if( GPendingLevel )
			SetProgress( "Cancelled Connect Attempt", "", 2.0 );
		else
			SetProgress( "", "", 0.0 );
		CancelPending();
		return 1;
	}
	else if( GLevel && GLevel->Exec( Cmd, Out ) )
	{
		return 1;
	}
	else if( UEngine::Exec( Cmd, Out ) )
	{
		return 1;
	}
	else return 0;
	unguard;
}

/*-----------------------------------------------------------------------------
	Serialization.
-----------------------------------------------------------------------------*/

//
// Serializer.
//
void UGameEngine::Serialize( FArchive& Ar )
{
	guard(UGameEngine::Serialize);
	UEngine::Serialize(Ar);

	Ar << GLevel << GEntry << GPendingLevel;

	unguardobj;
}

/*-----------------------------------------------------------------------------
	Game entering.
-----------------------------------------------------------------------------*/

//
// Cancel pending level.
//
void UGameEngine::CancelPending()
{
	guard(UGameEngine::CancelPending);
	if( GPendingLevel )
	{
		delete GPendingLevel;
		GPendingLevel = NULL;
	}
	unguard;
}

//
// Match Viewports to actors.
//
static void MatchViewportsToActors( UClient* Client, ULevel* Level, const FURL& URL )
{
	guard(MatchViewportsToActors);
	for( INT i=0; i<Client->Viewports.Num(); i++ )
	{
		char Error256[256]="";
		UViewport* Viewport = Client->Viewports(i);
		debugf( NAME_Log, "Spawning new actor for Viewport %s", Viewport->GetName() );
		if( !Level->SpawnPlayActor( Viewport, ROLE_SimulatedProxy, URL, Viewport->TravelItems, Error256 ) )
			appErrorf( Error256 );
		Viewport->TravelItems = "";
	}
	unguard;
}

//
// Browse to a specified URL, relative to the current one.
//
UBOOL UGameEngine::Browse( FURL URL, char* Error256 )
{
	guard(UGameEngine::Browse);
	// Includes new game, load game and console OPEN/START/TIMEDEMO. The initial
	// automatic TIMEDEMO arms its state only after this Browse has returned.
	CancelAutoTimedemo( "level travel requested" );
	check(Error256);
	Error256[0]=0;
	const char* Option;

	// Crack the URL.
	FString UrlStr;
	const char* StringURL=NULL;
	guard(Message);
	URL.String(UrlStr);
	StringURL = *UrlStr;
	debugf( "Browse: %s", StringURL );
	unguard;
	if( !URL.Valid )
	{
		// Unknown URL.
		guard(UnknownURL);
		appSprintf( Error256, LocalizeError("InvalidUrl"), StringURL );
		unguard;
		return 0;
	}
	else if( URL.HasOption("failed") )
	{
		// Handle failure URL.
		guard(FailedURL);
		debugf( NAME_Log, LocalizeError("AbortToEntry") );
		GLevel = GEntry;
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
		check(Client && Client->Viewports.Num());
		MatchViewportsToActors( Client, GLevel, URL );
		if( Audio && Client->Viewports.Num() )
			Audio->SetViewport( Client->Viewports(0) );
		GObj.CollectGarbage( GSystem, RF_Intrinsic );
		unguard;
		return 1;
	}
	else if( URL.HasOption("pop") )
	{
		// Pop the hub.
		guard(PopURL);
		if( GLevel && GLevel->GetLevelInfo()->HubStackLevel>0 )
		{
			char Filename[256], SavedPortal[256];
			appSprintf( Filename, "%s" PATH_SEPARATOR "Game%i.usa", PATH(GSys->SavePath), GLevel->GetLevelInfo()->HubStackLevel-1 );
			appStrcpy( SavedPortal, *URL.Portal );
			URL = FURL( &URL, Filename, TRAVEL_Partial );
			URL.Portal = SavedPortal;
		}
		else return 0;
		unguard;
	}
	else if( URL.HasOption("restart") )
	{
		// Handle restarting.
		guard(RestartURL);
		URL = LastURL;
		unguard;
	}
	else if( (Option=URL.GetOption("load=",NULL))!=NULL )
	{
		// Handle restarting.
		guard(LoadURL);
		char Temp[256], Error256[256];
		appSprintf( Temp, "%s" PATH_SEPARATOR "Save%i.usa", PATH(GSys->SavePath), appAtoi(Option) );
#ifdef PLATFORM_AMIGA
		// FURL's text parser treats forward slashes as URL/portal separators.
		// Passing "../Save/Save9.usa?load" through it therefore resets Map to
		// the default Index.unr.  Build a local-file URL directly instead.
		FURL SaveURL( Temp );
		SaveURL.AddOption( "load" );
		if( LoadMap(SaveURL,NULL,Error256) )
#else
		appStrcat( Temp, "?load" );
		if( LoadMap(FURL(&LastURL,Temp,TRAVEL_Partial),NULL,Error256) )
#endif
		{
			// Copy the hub stack.
			INT i;
			for( i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
			{
				char Src[256], Dest[256];
				appSprintf( Src, "%s" PATH_SEPARATOR "Save%i%i.usa", PATH(GSys->SavePath), appAtoi(Option), i );
				appSprintf( Dest, "%s" PATH_SEPARATOR "Game%i.usa", PATH(GSys->SavePath), i );
				appCopyFile( Src, Dest );
			}
			while( 1 )
			{
				appSprintf( Temp, "%s" PATH_SEPARATOR "Game%i.usa", PATH(GSys->SavePath), i++ );
				if( appFSize(Temp)<=0 )
					break;
				appUnlink( Temp );
			}
			LastURL = GLevel->URL;
			return 1;
		}
		else return 0;
		unguard;
	}

	// Handle normal URL's.
	if( URL.IsLocalInternal() )
	{
		// Local map file.
		guard(LocalMapURL);
		return LoadMap( URL, NULL, Error256 )!=NULL;
		unguard;
	}
	else if( URL.IsInternal() && GIsClient && (!GLevel || !GLevel->NetDriver || GLevel->NetDriver->ServerConnection) )
	{
		// Network URL.
		guard(NetworkURL);
		if( GPendingLevel )
			CancelPending();
		char Msg1[256], Msg2[256];
		appSprintf( Msg1, "Connecting (F10 Cancels):" );
		appSprintf( Msg2, "unreal://%s/%s", *URL.Host, *URL.Map );
		SetProgress( Msg1, Msg2, 60.0 );
		GPendingLevel = new UPendingLevel( this, URL );
		if( !GPendingLevel->NetDriver )
		{
			SetProgress( "Networking Failed", GPendingLevel->Error256, 6.0 );
			delete GPendingLevel;
			GPendingLevel = NULL;
		}
		return 0;
		unguard;
	}
	else if( URL.IsInternal() )
	{
		// Invalid.
		guard(InvalidURL);
		appSprintf( Error256, LocalizeError("ServerOpen") );
		unguard;
		return 0;
	}
	else
	{
		// External URL.
		guard(ExternalURL);
		appLaunchURL( StringURL, "", Error256 );
		unguard;
		return 0;
	}
	unguard;
}

//
// Load a map.
//
ULevel* UGameEngine::LoadMap( const FURL& URL, UPendingLevel* Pending, char* Error256 )
{
	guard(UGameEngine::LoadMap);
	CancelAutoTimedemo( "loading a map" );
	check(!GIsEditor);
	Error256[0]=0;
	FString Str;
	URL.String(Str);
	debugf( NAME_Log, "LoadMap: %s", *Str );

	// Remember current level's stack level.
	INT SavedHubStackLevel = GLevel ? GLevel->GetLevelInfo()->HubStackLevel : 0;

	// Display loading screen.
	guard(LoadingScreen);
	if( Client && Client->Viewports.Num() && GLevel )
	{
		GLevel->GetLevelInfo()->LevelAction = LEVACT_Loading;
		PaintProgress();
		if( Audio )
			Audio->SetViewport( Client->Viewports(0) );
		GLevel->GetLevelInfo()->LevelAction = LEVACT_None;
	}
	unguard;

	// Verify that we can load all packages we need.
	FGuid* Guid = NULL;
	UObject* MapParent = NULL;
	guard(VerifyPackages);
	try
	{
		if( Pending )
		{
			UNetConnection* Connection = Pending->NetDriver->ServerConnection;
			for( INT i=0; i<Connection->Driver->Map.Num(); i++ )
				GObj.GetPackageLinker( Connection->Driver->Map(i).Parent, NULL, LOAD_Verify | LOAD_Throw | LOAD_KeepImports | LOAD_NoWarn, NULL, &Connection->Driver->Map(i).Guid );
			if( Connection->Driver->Map.Num() )
			{
				MapParent = Connection->Driver->Map(0).Parent;
				Guid = &Connection->Driver->Map(0).Guid;
			}
		}
#ifdef PLATFORM_AMIGA
		AmigaDebugLogf( "[Amiga] LoadMap: VERIFY loading '%s'...", PATH(*URL.Map) );
#endif
		LoadObject<ULevel>( MapParent, "MyLevel", PATH(*URL.Map), LOAD_Verify | LOAD_Throw | LOAD_KeepImports | LOAD_NoWarn, NULL );
#ifdef PLATFORM_AMIGA
		AmigaDebugLogf( "[Amiga] LoadMap: VERIFY done." );
#endif
	}
	catch( char* Error )
	{
		// Safely failed loading.
#ifdef PLATFORM_AMIGA
		AmigaDebugLogf( "[Amiga] LoadMap: VERIFY threw: %s", Error );
#endif
		appStrcpy( Error256, Error );
		SetProgress( "Failed To Load Map", Error, 6.0 );
		return NULL;
	}
	unguard;
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] LoadMap: past verify, into cleanup/dissociate..." );
#endif

	// Dissociate Viewport actors.
	guard(DissociateViewports);
	if( Client )
	{
		for( INT i=0; i<Client->Viewports.Num(); i++ )
		{
			APlayerPawn* Actor          = Client->Viewports(i)->Actor;
			ULevel*      Level          = Actor->XLevel;
			Actor->Player               = NULL;
			Client->Viewports(i)->Actor = NULL;
			Level->DestroyActor( Actor );
		}
	}
	unguard;

	// Clean up game state.
	guard(ExitLevel);
	if( GLevel )
	{
		// Shut down.
		GObj.ResetLoaders( GLevel->GetParent() );
		if( GLevel->BrushTracker )
		{
			GLevel->BrushTracker->Exit();
			delete GLevel->BrushTracker;
			GLevel->BrushTracker = NULL;
		}
		if( GLevel->NetDriver )
		{
			delete GLevel->NetDriver;
			GLevel->NetDriver = NULL;
		}
		if( URL.HasOption("push") )
		{
			// Save the current level sans players actors.
			GLevel->CleanupDestroyed( 1 );
			char Filename[256];
			appSprintf( Filename, "%s" PATH_SEPARATOR "Game%i.usa", PATH(GSys->SavePath), SavedHubStackLevel );
			GObj.SavePackage( GLevel->GetParent(), GLevel, 0, Filename );
		}
		GLevel = NULL;
	}
	unguard;

	// Load all packages we need.
	guard(LoadLevel);
	if( MapParent && Guid )
		GObj.GetPackageLinker( MapParent, NULL, LOAD_Verify | LOAD_Throw | LOAD_KeepImports | LOAD_NoWarn, NULL, Guid );
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] LoadMap: loading level object '%s'...", PATH(*URL.Map) );
#endif
	GLevel = LoadObject<ULevel>( MapParent, "MyLevel", PATH(*URL.Map), LOAD_KeepImports | LOAD_NoFail, NULL );
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] LoadMap: level object = %p", (void*)GLevel );
#endif
	check(!GLevel->NetDriver);
	unguard;

	// Setup network package info.
	if( Pending )
	{
		if( Pending->LonePlayer )
		{
			Pending = NULL;
		}
		else
		{
			Pending->NetDriver->ServerConnection->Driver->Map.Compute();
		}
	}

	// Verify classes.
	guard(VerifyClasses);
	VERIFY_CLASS_OFFSET( A, Actor,       Owner         );
	VERIFY_CLASS_OFFSET( A, Actor,       TimerCounter  );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  Player        );
	VERIFY_CLASS_OFFSET( A, PlayerPawn,  MaxStepHeight );
	unguard;

	// Get LevelInfo.
	check(GLevel);
	ALevelInfo* Info = GLevel->GetLevelInfo();
	appStrcpy( Info->ComputerName, GComputerName );

	// Handle pushing.
	guard(ProcessHubStack);
	Info->HubStackLevel
	=	URL.HasOption("load") ? Info->HubStackLevel
	:	URL.HasOption("push") ? SavedHubStackLevel+1
	:	URL.HasOption("pop" ) ? Max(SavedHubStackLevel-1,0)
	:	URL.HasOption("peer") ? SavedHubStackLevel
	:	                        0;
	unguard;

	// Handle pending level.
	guard(ActivatePending);
	if( Pending )
	{
		check(Pending==GPendingLevel);

		// Hook network driver up to level.
		GLevel->NetDriver         = Pending->NetDriver;
		GLevel->NetDriver->Notify = GLevel;

		// Setup level.
		GLevel->GetLevelInfo()->NetMode    = NM_Client;
		GLevel->GetLevelInfo()->bInternet  = GLevel->NetDriver->IsInternet();
	}
	else check(!GLevel->NetDriver);
	unguard;

	// Set level info.
	guard(InitLevel);
	if( !URL.GetOption("load",NULL) )
		GLevel->URL = URL;
	appStrncpy( Info->EngineVersion, "1.0", ARRAY_COUNT(Info->EngineVersion) );
	GLevel->Engine = this;
	unguard;

	// Purge unused objects and flush caches.
	guard(Cleanup);
	Flush();
	GObj.CollectGarbage( GSystem, RF_Intrinsic );
	unguard;

	// Init collision.
	GLevel->SetActorCollision( 1 );

	// Setup zone distance table for sound damping.
	guard(SetupZoneTable);
	QWORD OldConvConn[64];
	QWORD ConvConn[64];
	INT i, j;
	for( i=0; i<64; i++ )
	{
		for ( INT j=0; j<64; j++ )
		{
			OldConvConn[i] = GLevel->Model->Nodes->Zones[i].Connectivity;
			if( i == j )
				GLevel->ZoneDist[i][j] = 0;
			else
				GLevel->ZoneDist[i][j] = 255;
		}
	}
	for( i=1; i<64; i++ )
	{
		for( j=0; j<64; j++ )
			for( INT k=0; k<64; k++ )
				if( (GLevel->ZoneDist[j][k] > i) && ((OldConvConn[j] & ((QWORD)1 << k)) != 0) )
					GLevel->ZoneDist[j][k] = i;
		for( j=0; j<64; j++ )
			ConvConn[j] = 0;
		for( j=0; j<64; j++ )
			for( INT k=0; k<64; k++ )
				if( (OldConvConn[j] & ((QWORD)1 << k)) != 0 )
					ConvConn[j] = ConvConn[j] | OldConvConn[k];
		for( j=0; j<64; j++ )
			OldConvConn[j] = ConvConn[j];
	}
	unguard;

	// Init the game info.
	char Options[1024]="";
	char Error256[256]="";
	char GameClassName[256]="";
	guard(InitGameInfo);
	for( INT i=0; i<URL.Op.Num(); i++ )
	{
		appStrcat( Options, "?" );
		appStrcat( Options, *URL.Op(i) );
		Parse( *URL.Op(i), "GAME=", GameClassName, ARRAY_COUNT(GameClassName) );
	}
	if( GLevel->IsServer() && !Info->Game )
	{
		// Get the GameInfo class.
		UClass* GameClass=NULL;
		if( !GameClassName[0] )
		{
			GameClass=Info->DefaultGameType;
			if( !GameClass )
				GameClass = GObj.LoadClass( AGameInfo::StaticClass, NULL, Client ? "ini:Engine.Engine.DefaultGame" : "ini:Engine.Engine.DefaultServerGame", NULL, LOAD_NoFail | LOAD_KeepImports, GLevel->GetSandbox() );
		}
		else GameClass = GObj.LoadClass( AGameInfo::StaticClass, NULL, GameClassName, NULL, LOAD_NoFail | LOAD_KeepImports, GLevel->GetSandbox() );

		// Spawn the GameInfo.
		debugf( NAME_Log, "Game class is '%s'", GameClass->GetName() );
		Info->Game = (AGameInfo*)GLevel->SpawnActor( GameClass );
		check(Info->Game!=NULL);
	}
	unguard;

	// Listen for clients.
	guard(Listen);
	if( !Client || URL.HasOption("Listen") )
	{
		char Error256[256];
		if( !GLevel->Listen( Error256 ) )
			appErrorf( LocalizeError("ServerListen"), Error256 );
	}
	unguard;

	// Init detail.
	Info->bHighDetailMode = 1;
	if
	(	Client
	&&	Client->Viewports.Num()
	&&	Client->Viewports(0)->RenDev
	&&	!Client->Viewports(0)->RenDev->HighDetailActors )
		Info->bHighDetailMode = 0;

	// Init level gameplay info.
	guard(BeginPlay);
	GLevel->iFirstDynamicActor = 0;
	if( !Info->bBegunPlay )
	{
		// Lock the level.
		debugf( NAME_Log, "Bringing %s up for play...", GLevel->GetFullName() );
		GLevel->TimeSeconds = 0;
		GLevel->GetLevelInfo()->TimeSeconds = 0;

		// Init touching actors.
		INT i;
		for( i=0; i<GLevel->Num(); i++ )
			if( GLevel->Actors(i) )
				for( INT j=0; j<ARRAY_COUNT(GLevel->Actors(i)->Touching); j++ )
					GLevel->Actors(i)->Touching[j] = NULL;

		// Handle network issues.
		if( !GLevel->IsServer() )
		{
			// Kill off actors that aren't interesting to the client.
			for( INT i=0; i<GLevel->Num(); i++ )
			{
				AActor* Actor = GLevel->Actors(i);
				if( Actor )
				{
					if( Actor->bStatic || Actor->bNoDelete )
						Exchange( Actor->Role, Actor->RemoteRole );
					else
						GLevel->DestroyActor( Actor );
				}
			}
		}

		// Init scripting.
		for( i=0; i<GLevel->Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->InitExecution();

		// Enable actor script calls.
		Info->bBegunPlay = 1;
		Info->bStartup = 1;

		// Init the game.
		if( Info->Game )
			Info->Game->eventInitGame( Options, Error256 );

		// Send PreBeginPlay.
		for( i=0; i<GLevel->Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventPreBeginPlay();

		// Set BeginPlay.
		for( i=0; i<GLevel->Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventBeginPlay();

		// Set zones.
		for( i=0; i<GLevel->Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->SetActorZone( GLevel->Actors(i), 1, 1 );

		// Post begin play.
		for( i=0; i<GLevel->Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventPostBeginPlay();

		// Begin scripting.
		for( i=0; i<GLevel->Num(); i++ )
			if( GLevel->Actors(i) )
				GLevel->Actors(i)->eventSetInitialState();

		// Find bases
		for( i=0; i<GLevel->Num(); i++ )
		{
			if( GLevel->Actors(i) && !GLevel->Actors(i)->Base && GLevel->Actors(i)->bCollideWorld 
				 && (GLevel->Actors(i)->IsA(ADecoration::StaticClass) || GLevel->Actors(i)->IsA(AInventory::StaticClass) || GLevel->Actors(i)->IsA(APawn::StaticClass)) 
				 &&	((GLevel->Actors(i)->Physics == PHYS_None) || (GLevel->Actors(i)->Physics == PHYS_Rotating)) )
			{
				 GLevel->Actors(i)->FindBase();
				 if ( GLevel->Actors(i)->Base == Info )
					 GLevel->Actors(i)->SetBase(NULL, 0);
			}
		}
		Info->bStartup = 0;
	}
	unguard;

	// Rearrange actors: static first, then others.
	guard(Rearrange);
	TArray<AActor*> Actors;
	Actors.AddItem(GLevel->Element(0));
	Actors.AddItem(GLevel->Element(1));
	INT i;
	for( i=2; i<GLevel->Num(); i++ )
		if( GLevel->Element(i) && GLevel->Element(i)->bStatic )
			Actors.AddItem( GLevel->Element(i) );
	GLevel->iFirstDynamicActor=Actors.Num();
	for( i=2; i<GLevel->Num(); i++ )
		if( GLevel->Element(i) && !GLevel->Element(i)->bStatic )
			Actors.AddItem( GLevel->Element(i) );
	GLevel->Empty();
	GLevel->Add( Actors.Num() );
	for( i=0; i<Actors.Num(); i++ )
		GLevel->Element(i) = Actors(i);
	unguard;

	// Cleanup profiling.
#if DO_SLOW_GUARD
	guard(CleanupProfiling);
	for( TObjectIterator<UFunction> It; It; ++It )
		It->Calls = It->Cycles=0;
	GTicks=1;
	unguard;
#endif

	// Client init.
	guard(ClientInit);
	if( Client )
	{
		// Match Viewports to actors.
		MatchViewportsToActors( Client, GLevel->IsServer() ? GLevel : GEntry, URL );

		// Reset input.
		for( INT i=0; i<Client->Viewports.Num(); i++ )
			Client->Viewports(i)->Input->ResetInput();

		// Init brush tracker.
		GLevel->BrushTracker = GNewBrushTracker( GLevel );

		// Set up audio.
		if( Audio && Client->Viewports.Num()>0 )
			Audio->SetViewport( Client->Viewports(0) );
	}
	unguard;

#ifdef PLATFORM_AMIGA
	// One-shot state dump for the currently invisible Brute family. This runs
	// only at LoadMap completion, never in the frame/render loop.
	for( INT AmigaActorIndex=0; AmigaActorIndex<GLevel->Num(); ++AmigaActorIndex )
	{
		AActor* AmigaActor = GLevel->Element(AmigaActorIndex);
		if( !AmigaActor )
			continue;
		const char* AmigaClassName = AmigaActor->GetClass()->GetName();
		if
		(	appStricmp(AmigaClassName,"Brute")!=0
		&&	appStricmp(AmigaClassName,"LesserBrute")!=0
		&&	appStricmp(AmigaClassName,"Behemoth")!=0 )
			continue;
		UMesh* AmigaMesh = AmigaActor->Mesh;
		debugf( NAME_Log,
			"AMIGA_BRUTE actor=%s class=%s hidden=%d hiddenEd=%d highDetail=%d onlyOwner=%d drawType=%d style=%d mesh=%s skin=%s texture=%s anim=%s frame=%.4f scale=%.4f fatness=%d verts=%d tris=%d meshTextures=%d",
			AmigaActor->GetName(), AmigaClassName,
			(INT)AmigaActor->bHidden, (INT)AmigaActor->bHiddenEd,
			(INT)AmigaActor->bHighDetail, (INT)AmigaActor->bOnlyOwnerSee,
			(INT)AmigaActor->DrawType, (INT)AmigaActor->Style,
			AmigaMesh ? AmigaMesh->GetName() : "None",
			AmigaActor->Skin ? AmigaActor->Skin->GetName() : "None",
			AmigaActor->Texture ? AmigaActor->Texture->GetName() : "None",
			*AmigaActor->AnimSequence,
			(DOUBLE)AmigaActor->AnimFrame, (DOUBLE)AmigaActor->DrawScale,
			(INT)AmigaActor->Fatness,
			AmigaMesh ? AmigaMesh->FrameVerts : 0,
			AmigaMesh ? AmigaMesh->Tris.Num() : 0,
			AmigaMesh ? AmigaMesh->Textures.Num() : 0 );
	}
#endif

	// Init detail.
	GLevel->DetailChange( Info->bHighDetailMode );

	// Remember the URL.
	guard(RememberURL);
	LastURL = URL;
	unguard;

	// Successfully started local level.
#ifdef PLATFORM_AMIGA
	AmigaDebugLogf( "[Amiga] LoadMap: SUCCESS, returning GLevel=%p", (void*)GLevel );
#endif
	return GLevel;
	unguard;
}

/*-----------------------------------------------------------------------------
	Game Viewport functions.
-----------------------------------------------------------------------------*/

//
// Draw a global view.
//
void UGameEngine::Draw( UViewport* Viewport, BYTE* HitData, INT* HitSize )
{
	guard(UGameEngine::Draw);
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw enter" );
#endif

	// Get view location.
	AActor*      ViewActor    = Viewport->Actor;
	FVector      ViewLocation = ViewActor->Location;
	FRotator     ViewRotation = ViewActor->Rotation;
	Viewport->Actor->eventPlayerCalcView( ViewActor, ViewLocation, ViewRotation );
	check(ViewActor);

	// See if viewer is inside world.
	DWORD LockFlags=0;
	FCheckResult Hit;
	if( !GLevel->Model->PointCheck(Hit,NULL,ViewLocation,FVector(0,0,0),0) )
		LockFlags |= LOCKR_ClearScreen;
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw after view/point-check lockflags=%lu", (unsigned long)LockFlags );
#endif

	// Lock the Viewport.
	check(Render);
	FPlane FlashScale = Client->ScreenFlashes ? 0.5*Viewport->Actor->FlashScale : FVector(0.5,0.5,0.5);
	FPlane FlashFog   = Client->ScreenFlashes ? Viewport->Actor->FlashFog : FVector(0,0,0);
	FlashScale.X = Clamp( FlashScale.X, 0.f, 1.f );
	FlashScale.Y = Clamp( FlashScale.Y, 0.f, 1.f );
	FlashScale.Z = Clamp( FlashScale.Z, 0.f, 1.f );
	FlashFog.X   = Clamp( FlashFog.X  , 0.f, 1.f );
	FlashFog.Y   = Clamp( FlashFog.Y  , 0.f, 1.f );
	FlashFog.Z   = Clamp( FlashFog.Z  , 0.f, 1.f );
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw before viewport-lock" );
#endif
	if( !Viewport->Lock(FlashScale,FlashFog,FPlane(0,0,0,0),LockFlags,HitData,HitSize) )
	{
		debugf( NAME_Warning, "Couldn't lock Viewport for drawing" );
		return;
	}
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw after viewport-lock" );
#endif

	// Setup rendering coords.
	FMemMark SceneMark(GSceneMem);
	FSceneNode* Frame = Render->CreateMasterFrame( Viewport, ViewLocation, ViewRotation, NULL );

	// Update level audio.
	if( Audio )
	{
		uclock(GLevel->AudioTickCycles);
		Audio->Update( ViewActor->Region, Frame->Coords );
		uunclock(GLevel->AudioTickCycles);
	}
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw after audio-update" );
#endif
	FMemMark MemMark(GMem);
	FMemMark DynMark(GDynMem);

	// Render.
	Render->PreRender( Frame );
	if( Viewport->Console )
		Viewport->Console->PreRender( Frame );
	Viewport->Canvas->Update( Frame );
	Viewport->Actor->eventPreRender( Viewport->Canvas );
	if( Frame->X>0 && Frame->Y>0 )
	{
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
			AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw before DrawWorld" );
#endif
		Render->DrawWorld( Frame );
	}
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw after DrawWorld" );
#endif
	Viewport->RenDev->EndFlash();
	Viewport->Actor->eventPostRender( Viewport->Canvas );
	if( Viewport->Console )
		Viewport->Console->PostRender( Frame );
	Render->PostRender( Frame );
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw after post-render/HUD" );
#endif

	// Done.
	#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw before viewport-unlock" );
	#endif
	Viewport->Unlock( 1 );
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw after viewport-unlock" );
#endif
	MemMark.Pop();
	DynMark.Pop();
	SceneMark.Pop();
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE GameDraw leave" );
#endif

	unguard;
}

void ExportTravel( FOutputDevice& Out, AActor* Actor )
{
	guard(ExportTravel);
	check(Actor);
	if( !Actor->bTravel )
		return;
	Out.Logf( "Class=%s Name=%s\r\n{\r\n", Actor->GetClass()->GetPathName(), Actor->GetName() );
	for( TFieldIterator<UProperty> It(Actor->GetClass()); It; ++It )
	{
		for( INT Index=0; Index<It->ArrayDim; Index++ )
		{
			char Value[1024];
			if
			(	(It->PropertyFlags & CPF_Travel)
			&&	It->ExportText( Index, Value, (BYTE*)Actor, &Actor->GetClass()->Defaults(0), 0 ) )
			{
				Out.Log( It->GetName() );
				if( It->ArrayDim!=1 )
					Out.Logf( "[%i]", Index );
				Out.Log( "=" );
				UObjectProperty* Ref = Cast<UObjectProperty>( *It );
				if( Ref && Ref->PropertyClass->IsChildOf(AActor::StaticClass) )
				{
					UObject* Obj = *(UObject**)( (BYTE*)Actor + It->Offset + Index*It->GetElementSize() );
					Out.Logf( "%s\r\n", Obj ? Obj->GetName() : "None" );
				}
				Out.Logf( "%s\r\n", Value );
			}
		}
	}
	Out.Logf( "}\r\n" );
	unguard;
}

//
// Jumping viewport.
//
void UGameEngine::SetClientTravel( UPlayer* Player, const char* NextURL, UBOOL bURL, UBOOL bItems, ETravelType TravelType )
{
	guard(UGameEngine::SetClientTravel);
	if( !Player && Client && Client->Viewports.Num() )
	{
		Player = Client->Viewports(0);
	}
	if( Player )
	{
		if( NextURL && appStricmp(NextURL,"?RESTART")==0 && Player->Actor && Player->Actor->CarryInfo )
		{
			// Automatically carry items the player had at start.
			Player->TravelItems = Player->Actor->CarryInfo->Text;
		}
		else if( bItems )
		{
			// Export items and self.
			FStringOut CarryInfo;
			ExportTravel( CarryInfo, Player->Actor );
			for( AActor* Inv=Player->Actor->Inventory; Inv; Inv=Inv->Inventory )
				ExportTravel( CarryInfo, Inv );
			Player->TravelItems = CarryInfo;
		}
		if( bURL && Cast<UViewport>(Player) )
		{
			// Set next URL.
			Cast<UViewport>(Player)->TravelURL = NextURL;
			Cast<UViewport>(Player)->TravelType = TravelType;
		}
	}
	unguard;
}

/*-----------------------------------------------------------------------------
	Tick.
-----------------------------------------------------------------------------*/

//
// Get tick rate limitor.
//
INT UGameEngine::GetMaxTickRate()
{
	guard(UEngine::GetMaxTickRate);
	if( GLevel && GLevel->NetDriver && !GLevel->NetDriver->ServerConnection )
		return GLevel->NetDriver->MaxTicksPerSecond;
	else
		return 0;
	unguard;
}

//
// Update everything.
//
void UGameEngine::Tick( FLOAT DeltaSeconds )
{
	guard(UGameEngine::Tick);
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: EngineTick enter" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST EngineTick enter" );
	}
#endif
	INT LocalTickCycles=0;
	uclock(LocalTickCycles);
#ifdef PLATFORM_AMIGA
	const DWORD AmigaPerfTickStart = SDL_GetTicks();
	DWORD AmigaPerfLevelStart = AmigaPerfTickStart;
	DWORD AmigaPerfLevelEnd = AmigaPerfTickStart;
	DWORD AmigaPerfClientStart = AmigaPerfTickStart;
	DWORD AmigaPerfClientEnd = AmigaPerfTickStart;
#endif

	// If all viewports closed, time to exit.
	if( Client && Client->Viewports.Num()==0 )
	{
		debugf("All Windows Closed");
		appRequestExit();
		return;
	}

	// The script aborts the demo when the menu opens and restores the old HUD.
	// Cancel the native watchdog as well, BEFORE processing a result/timeout,
	// including when Escape was pressed on the last frame of the benchmark.
	if( AutoTimedemoActive && Client && Client->Viewports.Num() )
	{
		APlayerPawn* Player = Client->Viewports(0)->Actor;
		if( Player && Player->bShowMenu )
			CancelAutoTimedemo( "menu opened" );
		else if( AutoTimedemoHUD && (!Player || Player->myHUD != AutoTimedemoHUD) )
			CancelAutoTimedemo( "benchmark HUD removed" );
	}

	// Unattended benchmark: quit once the TimeDemo HUD reports a completed
	// flyby cycle.  Measuring a whole cycle rather than a fixed time window
	// keeps runs comparable -- every run draws exactly the same frames along
	// the same camera path, so a difference in the result is a difference in
	// rendering speed and not in which part of the level got sampled.
	if( AutoTimedemoActive )
	{
		const DOUBLE WallNow = appSeconds();
		if( AutoTimedemoStart==0.0 )
		{
			AutoTimedemoStart = WallNow;
			// Start counting from the second tick: the first carries level
			// load time, not rendering.
			AutoTimedemoWallStart = WallNow;
			AutoTimedemoWallLast  = WallNow;
		}
		else
		{
			const DOUBLE FrameSecs = WallNow - AutoTimedemoWallLast;
			AutoTimedemoWallLast = WallNow;
			AutoTimedemoWallFrames++;
			if( FrameSecs > AutoTimedemoWorstFrame )
				AutoTimedemoWorstFrame = FrameSecs;
		}

		const UBOOL TimedOut =
			(WallNow-AutoTimedemoStart) >= (DOUBLE)AutoTimedemoTimeout;

		if( AutoTimedemoDone || TimedOut )
		{
#ifdef UE_AMIGA_GPROF
			// Preserve the profile even if subsequent renderer teardown faults.
			_moncleanup();
#endif
			// One timestamped file per run, so successive benchmarks pile up
			// instead of overwriting each other.
			char ResultDir[192] = "PROGDIR:fps_logs";
			GetConfigString( "Engine.GameEngine", "AutoTimedemoDir",
				ResultDir, ARRAY_COUNT(ResultDir) );
			appMkdir( ResultDir );		// harmless if it already exists

			INT Year, Month, DayOfWeek, Day, Hour, Min, Sec, MSec;
			appSystemTime( Year, Month, DayOfWeek, Day, Hour, Min, Sec, MSec );

			char ResultFile[256];
			appSprintf( ResultFile, "%s/fps_%04i%02i%02i_%02i%02i%02i.txt",
				ResultDir, Year, Month, Day, Hour, Min, Sec );

			const DOUBLE WallSecs = WallNow - AutoTimedemoWallStart;
			const DOUBLE WallFps  = WallSecs > 0.0
				? (DOUBLE)AutoTimedemoWallFrames / WallSecs : 0.0;
			// Worst single frame expressed as a rate; unlike the HUD's
			// "Minimum", this is not floored by the 100 ms simulation clamp.
			const DOUBLE WorstFps = AutoTimedemoWorstFrame > 0.0
				? 1.0 / AutoTimedemoWorstFrame : 0.0;

			FILE* F = fopen( ResultFile, "w" );
			if( F )
			{
				fprintf( F, "%04i-%02i-%02i %02i:%02i:%02i\n",
					Year, Month, Day, Hour, Min, Sec );
				if( AutoTimedemoDone )
					fprintf( F, "%s\n", AutoTimedemoResult );
				else
					fprintf( F, "TIMEOUT after %d seconds - flyby never completed\n",
						AutoTimedemoTimeout );
				// Wall-clock figures; see the note on AutoTimedemoWallFrames.
				// These differ from the line above whenever frames overran the
				// 100 ms simulation clamp.
				fprintf( F, "Wall: %.2f FPS (%d frames, %.2f seconds)\n",
					(FLOAT)WallFps, AutoTimedemoWallFrames, (FLOAT)WallSecs );
				fprintf( F, "Worst frame: %.0f ms (%.2f FPS)\n",
					(FLOAT)(AutoTimedemoWorstFrame*1000.0), (FLOAT)WorstFps );
				fclose( F );
			}
			else
			{
				debugf( "AutoTimedemo: could not write %s", ResultFile );
			}
			if( AutoTimedemoDone )
				debugf( "AutoTimedemo: %s -> %s", AutoTimedemoResult, ResultFile );
			else
				debugf( "AutoTimedemo: timed out after %i seconds", AutoTimedemoTimeout );

#ifdef ENGINE_USE_MINIGL
			// MiniGL may still reference the last frame's client-side geometry.
			// Drain it while the level and scene allocators are alive. If this is
			// deferred to render-device destruction, UE has already released that
			// memory and MiniGL's final draw/clip pass faults during glFinish().
			if( Client && Client->Viewports.Num() && Client->Viewports(0)->RenDev )
			{
				debugf( "AutoTimedemo: flushing MiniGL before engine teardown" );
				Client->Viewports(0)->RenDev->Flush();
				debugf( "AutoTimedemo: MiniGL flush complete" );
			}
#endif
			AutoTimedemoActive = 0;
			appRequestExit();
			return;
		}
	}

	// If game is paused, release the cursor.
	static UBOOL WasPaused=1;
	if( Client && Client->CaptureMouse && Client->Viewports.Num()==1 && GLevel && !Client->FullscreenViewport )
	{
		UBOOL IsPaused = (GLevel->GetLevelInfo()->Pauser[0]!=0) || (Client->Viewports(0)->Actor->bShowMenu);
		if( IsPaused && !WasPaused )
			Client->Viewports(0)->SetMouseCapture( 0, 0 );
		else if( WasPaused && !IsPaused )
			Client->Viewports(0)->SetMouseCapture( 1, 1, 1 );
		WasPaused = IsPaused;
	}
	else WasPaused=0;

	// Update subsystems.
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: EngineTick before object/cache tick" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST EngineTick before object/cache tick" );
	}
#endif
	GObj.Tick();				
	GCache.Tick();
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: EngineTick after object/cache tick" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST EngineTick after object/cache tick" );
	}
#endif

	// Update the level.
	guard(TickLevel);
	GameCycles=0;
	uclock(GameCycles);
#ifdef PLATFORM_AMIGA
	AmigaPerfLevelStart = SDL_GetTicks();
#endif
	if( GLevel )
		GLevel->Tick( LEVELTICK_All, DeltaSeconds );
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->Actor->XLevel!=GLevel )
		Client->Viewports(0)->Actor->XLevel->Tick( LEVELTICK_All, DeltaSeconds );
#ifdef PLATFORM_AMIGA
	AmigaPerfLevelEnd = SDL_GetTicks();
#endif
	uunclock(GameCycles);
	unguard;
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: EngineTick after level tick" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST EngineTick after level tick" );
	}
#endif

	// Handle server travelling.
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE EngineTick before server-travel" );
#endif
	guard(ServerTravel);
	if( GLevel && *GLevel->GetLevelInfo()->NextURL )
	{
		if( (GLevel->GetLevelInfo()->NextSwitchCountdown-=DeltaSeconds) <= 0.0 )
		{
			// Travel to new level, and exit.
			TArray<FString> TravelNames;
			TArray<FString>	TravelItems;
			for( INT i=0; i<GLevel->Num(); i++ )
			{
				APlayerPawn* P = Cast<APlayerPawn>( GLevel->Element(i) );
				if( P && P->Player )
				{
					P->Player->TravelItems="";
					if( Cast<UNetConnection>(P->Player) )
						SetClientTravel( P->Player, GLevel->GetLevelInfo()->NextURL, 1, GLevel->GetLevelInfo()->bNextItems, TRAVEL_Relative );
					if( Cast<UViewport>( P->Player ) )
						Cast<UViewport>( P->Player )->TravelURL = "";
					new(TravelNames)FString(P->PlayerName);
					new(TravelItems)FString(P->Player->TravelItems);
				}
			}
			debugf( "Server switch level: %s", GLevel->GetLevelInfo()->NextURL );
			char Error256[256];
			Browse( FURL(&LastURL,GLevel->GetLevelInfo()->NextURL,TRAVEL_Relative), Error256 );
			*GLevel->GetLevelInfo()->NextURL = 0;
			GLevel->TravelNames = TravelNames;
			GLevel->TravelItems = TravelItems;
			return;
		}
	}
	unguard;
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE EngineTick after server-travel" );
#endif

	// Handle client travelling.
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE EngineTick before client-travel" );
#endif
	guard(ClientTravel);
	if( Client && Client->Viewports.Num() && Client->Viewports(0)->TravelURL!="" )
	{
		// Travel to new level, and exit.
		FString NextURL = Client->Viewports(0)->TravelURL;
		Client->Viewports(0)->TravelURL="";
		char Error256[256];
		Browse( FURL(&LastURL,*NextURL,Client->Viewports(0)->TravelType), Error256 );
		return;
	}
	unguard;
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE EngineTick after client-travel" );
#endif

	// Update the pending level.
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE EngineTick before pending-level" );
#endif
	guard(TickPending);
	if( GPendingLevel )
	{
		GPendingLevel->Tick( DeltaSeconds );
		if( GPendingLevel && GPendingLevel->Error256[0] )
		{
			// Pending connect failed.
			guard(PendingFailed);
			FString Str;
			GPendingLevel->URL.String( Str );
			debugf( NAME_Log, LocalizeError("Pending"), *Str, GPendingLevel->Error256 );
			delete GPendingLevel;
			GPendingLevel = NULL;
			//!!should convey this failure to the user by an in-game message.
			unguard;
		}
		else if( GPendingLevel->Success && !GPendingLevel->FilesNeeded && !GPendingLevel->SentJoin )
		{
			// Attempt to load the map.
			char Error256[256];
			guard(AttemptLoadPending);
			LoadMap( GPendingLevel->URL, GPendingLevel, Error256 );
			if( Error256[0] )
			{
				//!!report the error.
			}
			else if( !GPendingLevel->LonePlayer )
			{
				GPendingLevel->SentJoin = 1;
				GPendingLevel->NetDriver->ServerConnection->Logf( "JOIN" );
				GPendingLevel->NetDriver->ServerConnection->FlushNet();
				GPendingLevel->NetDriver = NULL;
				GLevel->GetLevelInfo()->LevelAction = LEVACT_Connecting;
				GEntry->GetLevelInfo()->LevelAction = LEVACT_Connecting;
			}
			unguard;

			// Kill the pending level.
			guard(KillPending);
			delete GPendingLevel;
			GPendingLevel = NULL;
			unguard;
		}
	}
	unguard;
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
		AmigaDebugLogf( "[Amiga] AUTO TRACE EngineTick after pending-level" );
#endif

	// Render everything.
	guard(ClientTick);
	INT LocalClientCycles=0;
#ifdef PLATFORM_AMIGA
	AmigaPerfClientStart = SDL_GetTicks();
	AmigaPerfClientEnd = AmigaPerfClientStart;
#endif
	if( Client )
	{
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
		{
			debugf( NAME_Init, "FIRST: EngineTick before client tick" );
			AmigaDebugLogf( "[Amiga] AUTO FIRST EngineTick before client tick" );
		}
#endif
		uclock(LocalClientCycles);
		Client->Tick();
		uunclock(LocalClientCycles);
#ifdef PLATFORM_AMIGA
		if( GAmigaStartupTraceFrames > 0 )
		{
			debugf( NAME_Init, "FIRST: EngineTick after client tick" );
			AmigaDebugLogf( "[Amiga] AUTO FIRST EngineTick after client tick" );
		}
#endif
#ifdef PLATFORM_AMIGA
		AmigaPerfClientEnd = SDL_GetTicks();
#endif
	}
	ClientCycles=LocalClientCycles;
	unguard;

#ifdef PLATFORM_AMIGA
	// Stay completely passive during startup. Once the map has warmed up,
	// request exactly one per-class actor profile after a genuinely slow
	// simulation frame. The following frame is measured automatically, so
	// the user never has to restart merely to enable diagnostics.
	static ULevel* AmigaPerfLevel = NULL;
	static INT AmigaPerfWarmupFrames = 0;
	static INT AmigaPerfCooldownFrames = 0;
	if( GLevel != AmigaPerfLevel )
	{
		AmigaPerfLevel = GLevel;
		AmigaPerfWarmupFrames = 100;
		AmigaPerfCooldownFrames = 0;
	}
	const DWORD AmigaPerfEnd = SDL_GetTicks();
	const DWORD AmigaPerfLevelMs = AmigaPerfLevelEnd-AmigaPerfLevelStart;
	const DWORD AmigaPerfTotalMs = AmigaPerfEnd-AmigaPerfTickStart;
	if( AmigaPerfWarmupFrames > 0 )
		--AmigaPerfWarmupFrames;
	else if( AmigaPerfCooldownFrames > 0 )
		--AmigaPerfCooldownFrames;
	else if( GLevel && AmigaPerfLevelMs >= 150 && GAmigaActorProfileRequests==0 )
	{
		AmigaDebugLogf( "[Amiga] AUTOPERF trigger map=%s actors=%d level=%lu total=%lu ms",
			GLevel->GetName(), GLevel->Num(),
			(unsigned long)AmigaPerfLevelMs, (unsigned long)AmigaPerfTotalMs );
		GAmigaActorProfileRequests = 1;
		AmigaPerfCooldownFrames = 100;
	}
#endif

	uunclock(LocalTickCycles);
	TickCycles=LocalTickCycles;
	GTicks++;
#ifdef PLATFORM_AMIGA
	if( GAmigaStartupTraceFrames > 0 )
	{
		debugf( NAME_Init, "FIRST: EngineTick leave" );
		AmigaDebugLogf( "[Amiga] AUTO FIRST EngineTick leave" );
	}
#endif
	unguard;
}

/*-----------------------------------------------------------------------------
	Saving the game.
-----------------------------------------------------------------------------*/

//
// Save the current game state to a file.
//
void UGameEngine::SaveGame( INT Position )
{
	guard(UGameEngine::SaveGame);
	char Filename[256];
	appMkdir( PATH(GSys->SavePath) );
	appSprintf( Filename, "%s" PATH_SEPARATOR "Save%i.usa", PATH(GSys->SavePath), Position );
	GLevel->GetLevelInfo()->LevelAction=LEVACT_Saving;
	PaintProgress();
	GSystem->BeginSlowTask( LocalizeProgress("Saving"), 1, 0 );
	if( GLevel->BrushTracker )
	{
		GLevel->BrushTracker->Exit();
		delete GLevel->BrushTracker;
	}
	GLevel->CleanupDestroyed( 1 );
	if( GObj.SavePackage( GLevel->GetParent(), GLevel, 0, Filename ) )
	{
		// Copy the hub stack.
		INT i;
		for( i=0; i<GLevel->GetLevelInfo()->HubStackLevel; i++ )
		{
			char Src[256], Dest[256];
			appSprintf( Src, "%s" PATH_SEPARATOR "Game%i.usa", PATH(GSys->SavePath), i );
			appSprintf( Dest, "%s" PATH_SEPARATOR "Save%i%i.usa", PATH(GSys->SavePath), Position, i );
			appCopyFile( Src, Dest );
		}
		while( 1 )
		{
			appSprintf( Filename, "%s" PATH_SEPARATOR "Save%i%i.usa", PATH(GSys->SavePath), Position, i++ );
			if( appFSize(Filename)<=0 )
				break;
			appUnlink( Filename );
		}
	}
	for( INT i=0; i<GLevel->Num(); i++ )
		if( Cast<AMover>(GLevel->Actors(i)) )
			Cast<AMover>(GLevel->Actors(i))->SavedPos = FVector(-1,-1,-1);
	GLevel->BrushTracker = GNewBrushTracker( GLevel );
	GSystem->EndSlowTask();
	GLevel->GetLevelInfo()->LevelAction=LEVACT_None;

	unguard;
}

/*-----------------------------------------------------------------------------
	Mouse feedback.
-----------------------------------------------------------------------------*/

//
// Mouse delta while dragging.
//
void UGameEngine::MouseDelta( UViewport* Viewport, DWORD ClickFlags, FLOAT DX, FLOAT DY )
{
	guard(UGameEngine::MouseDelta);
	if( (ClickFlags & MOUSE_FirstHit) && Client && Client->Viewports.Num()==1 && GLevel && !Client->FullscreenViewport && GLevel->GetLevelInfo()->Pauser[0]==0 && !Viewport->Actor->bShowMenu )
	{
		Viewport->SetMouseCapture( 1, 1, 1 );
	}
	else if( (ClickFlags & MOUSE_LastRelease) && !Client->CaptureMouse )
	{
		Viewport->SetMouseCapture( 0, 0 );
	}
	unguard;
}

//
// Absolute mouse position.
//
void UGameEngine::MousePosition( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::MousePosition);
	unguard;
}

//
// Mouse clicking.
//
void UGameEngine::Click( UViewport* Viewport, DWORD ClickFlags, FLOAT X, FLOAT Y )
{
	guard(UGameEngine::Click);
	unguard;
}

/*-----------------------------------------------------------------------------
	The End.
-----------------------------------------------------------------------------*/
