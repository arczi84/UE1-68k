/*------------------------------------------------------------------------------------
	Dependencies.
------------------------------------------------------------------------------------*/

#include "AL/al.h"
#include "AL/alc.h"
#include "libmodplug/modplug.h"
#include "Engine.h"

/*------------------------------------------------------------------------------------
	OpenAL audio subsystem private definitions.
------------------------------------------------------------------------------------*/

#define MAX_SOURCES 64

#define NUM_MUSIC_BUFFERS 4

#define INVALID_SOURCE ((ALuint)-1)
#define INVALID_BUFFER ((ALuint)-1)

#define SOUND_SLOT_IS( Id, Slot ) ( ( (Id) & 14 ) == (Slot) * 2 )
#define AMBIENT_SOUND_ID( ActorIndex ) ( (ActorIndex) * 16 + SLOT_Ambient * 2 )

#define DEFAULT_OUTPUT_RATE 44100

#ifdef PLATFORM_AMIGA
// libmodplug returns quickly on 68k.  A 16 KiB block gives OpenAL/AHI about
// 186 ms at 22.05 kHz stereo, avoiding an underrun between slower frames.
#define STREAM_BUFSIZE 16384
#else
#define STREAM_BUFSIZE 32768
#endif

// World scale related constants, same as in ALAudio 2.4.7.
#define DISTANCE_SCALE 0.023255814f
#define ROLLOFF_FACTOR 1.1f
#define DESPATIALIZE_FACTOR 0.1f

class DLL_EXPORT UNOpenALAudioSubsystem : public UAudioSubsystem
{
	DECLARE_CLASS_WITHOUT_CONSTRUCT(UNOpenALAudioSubsystem, UAudioSubsystem, CLASS_Config)

	// Options
	char DeviceName[256];
	INT OutputRate;
	BYTE MasterVolume;
	BYTE SoundVolume;
	BYTE MusicVolume;
	BYTE MusicInterpolation;
	FLOAT AmbientFactor;
	FLOAT DopplerFactor;
	UBOOL UseReverb;
	UBOOL UseHRTF;

	// Constructors.
	static void InternalClassInitializer( UClass* Class );
	UNOpenALAudioSubsystem();

	// UObject interface.
	virtual void Serialize( FArchive& Ar ) override;
	virtual void Destroy() override;
	virtual void PostEditChange() override;
	virtual void ShutdownAfterError() override;

	// UAudioSubsystem interface.
	virtual UBOOL Init() override;
	virtual void SetViewport( UViewport* Viewport ) override;
	virtual UBOOL Exec( const char* Cmd, FOutputDevice* Out = GSystem ) override;
	virtual void Update( FPointRegion Region, FCoords& Listener ) override;
	virtual void RegisterMusic( UMusic* Music ) override;
	virtual void RegisterSound( USound* Music ) override;
	virtual void UnregisterSound( USound* Sound ) override;
	virtual void UnregisterMusic( UMusic* Music ) override;
	virtual UBOOL PlaySound( AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch ) override;
	virtual void NoteDestroy( AActor* Actor );
	virtual UBOOL GetLowQualitySetting() override { return false; };

	// Internals.
private:
	UViewport* Viewport;
	ALCdevice* Device;
	ALCcontext* Ctx;
	ALuint Sources[MAX_SOURCES];
	TArray<ALuint> Buffers;
	INT NextId;
	FCoords ListenerCoords;

	ALuint ReverbEffect;
	ALuint ReverbSlot;
	UBOOL ReverbOn;
	AZoneInfo* ReverbZone;

	ModPlugFile* MusicFile;
	UMusic* Music;
	FLOAT MusicFade;
	DOUBLE MusicTime;
	BYTE MusicSection;
	ALuint MusicSource;
	UBOOL MusicIsPlaying = false;
	UBOOL MusicIsLoaded = false;

	BYTE MusicBufferData[STREAM_BUFSIZE];
	ALuint MusicBuffers[NUM_MUSIC_BUFFERS];
	ALuint FreeMusicBuffers[NUM_MUSIC_BUFFERS];
	INT NumFreeMusicBuffers;
#ifdef PLATFORM_AMIGA
	INT StartupFadeStep;
#endif

	volatile UBOOL MusicThreadRunning;
	FMutex MusicMutex { "MusicMutex" };
	UTHREAD MusicThread;

	enum ENVoiceOp
	{
		NVOP_None,
		NVOP_Play,
		NVOP_Stop,
		NVOP_Pause,
	};

	struct FNVoice
	{
		ALuint Buffer = INVALID_BUFFER;
		AActor* Actor;
		INT Id;
		USound* Sound;
		FVector Location;
		FVector Velocity;
		FLOAT Volume;
		FLOAT Radius;
		FLOAT Pitch;
		FLOAT Priority;
		UBOOL Looping;
		UBOOL BufferChanged = false;
	} Voices[MAX_SOURCES];

	void InitReverbEffect();
	void UpdateReverb( FPointRegion& Region );
	void UpdateVoice( INT Num, const ENVoiceOp Op = NVOP_None );
	void StopVoice( INT Num );
	void PlayMusic();
	void StopMusic();

	void UpdateMusicBuffers();
	void ClearMusicBuffers();

	void StartMusicThread();
	void StopMusicThread();

	inline FLOAT GetVoicePriority( const FVector& Location, FLOAT Volume, FLOAT Radius )
	{
		if( Radius && Viewport->Actor )
			return Volume * ( 1.f - (Location - Viewport->Actor->Location).Size() / Radius );
		else
			return Volume;
	}

	#ifdef PLATFORM_WIN32
	static DWORD __stdcall MusicThreadProc( void* Audio );
	#else
	static void* MusicThreadProc( void* Audio );
	#endif
};
