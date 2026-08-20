#include <string.h>

#include "Engine.h"
#include "UnRender.h"
#include "SDL2/SDL.h"
#include "SDL_mixer.h"
#include "libmodplug/modplug.h"

#define MAX_MIX_CHANNELS 24
#define SOUND_SLOT_IS(Id,Slot) (((Id) & 14) == (Slot) * 2)
#define AMBIENT_SOUND_ID(ActorIndex) ((ActorIndex) * 16 + SLOT_Ambient * 2)

class DLL_EXPORT UNullAudioSubsystem : public UAudioSubsystem
{
	DECLARE_CLASS_WITHOUT_CONSTRUCT(UNullAudioSubsystem,UAudioSubsystem,CLASS_Config)

	INT OutputRate;
	BYTE SoundVolume;
	BYTE MusicVolume;
	UBOOL LowSoundQuality;
	FLOAT AmbientFactor;

	struct FVoice
	{
		AActor* Actor;
		USound* Sound;
		INT Id;
		FVector Location;
		FLOAT Volume;
		FLOAT Radius;
		FLOAT Pitch;
		FLOAT Priority;
		UBOOL Looping;
	};

	UViewport* Viewport;
	FCoords ListenerCoords;
	FVoice Voices[MAX_MIX_CHANNELS];
	INT NextId;
	UBOOL AudioOpen;

	ModPlugFile* MusicFile;
	UMusic* Music;
	BYTE MusicSection;
	UBOOL MusicLoaded;
	volatile UBOOL MusicPlaying;
	BYTE MusicMixBuffer[8192];
	BYTE MusicRingBuffer[65536];
	volatile INT MusicRingRead;
	volatile INT MusicRingWrite;
	volatile INT MusicRingCount;
	volatile INT MusicMixCalls;
	volatile INT MusicMixActiveBuffers;
	volatile INT MusicMixErrors;
	UBOOL MusicMixReported;

	static void SDLCALL MusicPostMix(void* Userdata, Uint8* Stream, int Length);
	void FillMusicBuffer();
	void StopVoice(INT Channel);
	void UpdateVoice(INT Channel);
	void StopMusic();
	void StartMusic();
	FLOAT VoicePriority(const FVector& Location, FLOAT Volume, FLOAT Radius) const;

public:
	static void InternalClassInitializer(UClass* Class);
	UNullAudioSubsystem();
	virtual UBOOL Init();
	virtual void Destroy();
	virtual void SetViewport(UViewport* InViewport);
	virtual UBOOL Exec(const char* Cmd, FOutputDevice* Out=GSystem);
	virtual void Update(FPointRegion Region, FCoords& Listener);
	virtual void RegisterMusic(UMusic* InMusic);
	virtual void RegisterSound(USound* Sound);
	virtual void UnregisterSound(USound* Sound);
	virtual void UnregisterMusic(UMusic* InMusic);
	virtual UBOOL PlaySound(AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch);
	virtual void NoteDestroy(AActor* Actor);
	virtual UBOOL GetLowQualitySetting();
};

void UNullAudioSubsystem::InternalClassInitializer(UClass* Class)
{
	guardSlow(UNullAudioSubsystem::InternalClassInitializer);
	new(Class,"OutputRate",RF_Public)UIntProperty(CPP_PROPERTY(OutputRate),"Audio",CPF_Config);
	new(Class,"SoundVolume",RF_Public)UByteProperty(CPP_PROPERTY(SoundVolume),"Audio",CPF_Config);
	new(Class,"MusicVolume",RF_Public)UByteProperty(CPP_PROPERTY(MusicVolume),"Audio",CPF_Config);
	new(Class,"LowSoundQuality",RF_Public)UBoolProperty(CPP_PROPERTY(LowSoundQuality),"Audio",CPF_Config);
	new(Class,"AmbientFactor",RF_Public)UFloatProperty(CPP_PROPERTY(AmbientFactor),"Audio",CPF_Config);
	unguardSlow;
}

UNullAudioSubsystem::UNullAudioSubsystem()
{
	OutputRate = 22050;
	SoundVolume = 224;
	MusicVolume = 160;
	LowSoundQuality = false;
	AmbientFactor = 0.2f;
	Viewport = NULL;
	NextId = 0;
	AudioOpen = false;
	MusicFile = NULL;
	Music = NULL;
	MusicSection = 0;
	MusicLoaded = false;
	MusicPlaying = false;
	MusicRingRead = 0;
	MusicRingWrite = 0;
	MusicRingCount = 0;
	MusicMixCalls = 0;
	MusicMixActiveBuffers = 0;
	MusicMixErrors = 0;
	MusicMixReported = false;
	for(INT i=0; i<MAX_MIX_CHANNELS; ++i)
	{
		Voices[i].Actor = NULL;
		Voices[i].Sound = NULL;
		Voices[i].Id = 0;
		Voices[i].Priority = 0.f;
		Voices[i].Looping = false;
	}
}

UBOOL UNullAudioSubsystem::Init()
{
	guard(UNullAudioSubsystem::Init);

	if(OutputRate < 11025 || OutputRate > 48000)
		OutputRate = 22050;
	AmbientFactor = Clamp(AmbientFactor,0.f,1.f);

	if(!(SDL_WasInit(SDL_INIT_AUDIO) & SDL_INIT_AUDIO) && SDL_InitSubSystem(SDL_INIT_AUDIO) < 0)
	{
		debugf(NAME_Warning,"SDL audio init failed: %s",SDL_GetError());
		return false;
	}

	if(Mix_OpenAudio(OutputRate,AUDIO_S16SYS,2,1024) < 0)
	{
		debugf(NAME_Warning,"SDL_mixer/AHI open failed: %s",Mix_GetError());
		return false;
	}
	AudioOpen = true;
	Mix_AllocateChannels(MAX_MIX_CHANNELS);

	INT ActualRate=0, ActualChannels=0;
	Uint16 ActualFormat=0;
	Mix_QuerySpec(&ActualRate,&ActualFormat,&ActualChannels);
	OutputRate = ActualRate;

	ModPlug_Settings Settings;
	ModPlug_GetSettings(&Settings);
	Settings.mFlags=0;
	Settings.mChannels=2;
	Settings.mBits=16;
	Settings.mFrequency=OutputRate;
	Settings.mResamplingMode=MODPLUG_RESAMPLE_LINEAR;
	Settings.mStereoSeparation=128;
	Settings.mMaxMixChannels=64;
	Settings.mLoopCount=-1;
	ModPlug_SetSettings(&Settings);
	Mix_SetPostMix(&UNullAudioSubsystem::MusicPostMix,this);

	USound::Audio = this;
	UMusic::Audio = this;
	debugf(NAME_Init,"SDL1/AHI audio initialized: %d Hz, format=%04x, channels=%d, voices=%d, music=%s",
		ActualRate,(INT)ActualFormat,ActualChannels,MAX_MIX_CHANNELS,"libmodplug");
	return true;

	unguard;
}

void UNullAudioSubsystem::Destroy()
{
	guard(UNullAudioSubsystem::Destroy);

	if(AudioOpen)
	{
		Mix_SetPostMix(NULL,NULL);
		Mix_HaltChannel(-1);
		StopMusic();
		for(TObjectIterator<USound> It; It; ++It)
			UnregisterSound(*It);
		if(MusicFile)
		{
			ModPlug_Unload(MusicFile);
			MusicFile = NULL;
			MusicLoaded = false;
		}
		Mix_CloseAudio();
		AudioOpen = false;
	}
	USound::Audio = NULL;
	UMusic::Audio = NULL;
	Super::Destroy();

	unguard;
}

void UNullAudioSubsystem::SetViewport(UViewport* InViewport)
{
	guard(UNullAudioSubsystem::SetViewport);
	if(InViewport != Viewport)
	{
		Mix_HaltChannel(-1);
		for(INT i=0; i<MAX_MIX_CHANNELS; ++i)
			StopVoice(i);
	}
	Viewport = InViewport;
	unguard;
}

UBOOL UNullAudioSubsystem::Exec(const char* Cmd, FOutputDevice* Out)
{
	guard(UNullAudioSubsystem::Exec);
	const char* Str=Cmd;
	if(ParseCommand(&Str,"AUDIOSTAT"))
	{
		INT Playing=0;
		for(INT i=0;i<MAX_MIX_CHANNELS;++i)
			Playing += Mix_Playing(i)!=0;
		Out->Logf("SDL/AHI: %d Hz, %d voices playing, music=%s",OutputRate,Playing,MusicPlaying ? "playing" : "stopped");
		return true;
	}
	return false;
	unguard;
}

static DWORD ReadLE32(const BYTE* Ptr)
{
	return (DWORD)Ptr[0] | ((DWORD)Ptr[1]<<8) | ((DWORD)Ptr[2]<<16) | ((DWORD)Ptr[3]<<24);
}

static UBOOL WaveHasLoop(const TArray<BYTE>& Data)
{
	if(Data.Num()<12 || appMemcmp(&Data(0),"RIFF",4)!=0 || appMemcmp(&Data(8),"WAVE",4)!=0)
		return false;
	INT Pos=12;
	while(Pos+8<=Data.Num())
	{
		const BYTE* Chunk=&Data(Pos);
		const DWORD Size=ReadLE32(Chunk+4);
		if(appMemcmp(Chunk,"smpl",4)==0 && Size>=36 && Pos+8+36<=Data.Num())
			return ReadLE32(Chunk+8+28)!=0;
		if(Size>(DWORD)Data.Num())
			break;
		Pos += 8 + (INT)((Size+1)&~1u);
	}
	return false;
}

void UNullAudioSubsystem::RegisterSound(USound* Sound)
{
	guard(UNullAudioSubsystem::RegisterSound);
	if(!AudioOpen || !Sound || Sound->Handle || !Sound->Data.Num())
		return;
	SDL_RWops* RW=SDL_RWFromMem(&Sound->Data(0),Sound->Data.Num());
	Mix_Chunk* Chunk=RW ? Mix_LoadWAV_RW(RW,1) : NULL;
	if(!Chunk)
	{
		debugf(NAME_Warning,"Could not decode sound %s: %s",Sound->GetName(),Mix_GetError());
		return;
	}
	Sound->Handle=Chunk;
	Sound->Looping=WaveHasLoop(Sound->Data);
	unguard;
}

void UNullAudioSubsystem::UnregisterSound(USound* Sound)
{
	guard(UNullAudioSubsystem::UnregisterSound);
	if(Sound && Sound->Handle)
	{
		for(INT i=0;i<MAX_MIX_CHANNELS;++i)
			if(Voices[i].Sound==Sound)
				StopVoice(i);
		Mix_FreeChunk((Mix_Chunk*)Sound->Handle);
		Sound->Handle=NULL;
	}
	unguard;
}

void UNullAudioSubsystem::RegisterMusic(UMusic* InMusic)
{
	guard(UNullAudioSubsystem::RegisterMusic);
	if(!InMusic || InMusic->Handle || !InMusic->Data.Num())
		return;
	SDL_LockAudio();
	if(MusicFile)
	{
		ModPlug_Unload(MusicFile);
		MusicFile=NULL;
		MusicLoaded=false;
	}
	MusicFile=ModPlug_Load(&InMusic->Data(0),InMusic->Data.Num());
	if(MusicFile)
	{
		InMusic->Handle=MusicFile;
		MusicLoaded=true;
		ModPlug_SetMasterVolume(MusicFile,Clamp((INT)MusicVolume*2,1,512));
	}
	SDL_UnlockAudio();
	if(!MusicFile)
		debugf(NAME_Warning,"Could not load music %s with libmodplug",InMusic->GetName());
	else
		debugf(NAME_Init,"Music loaded: %s (%d bytes)",InMusic->GetName(),InMusic->Data.Num());
	unguard;
}

void UNullAudioSubsystem::UnregisterMusic(UMusic* InMusic)
{
	guard(UNullAudioSubsystem::UnregisterMusic);
	if(InMusic && InMusic->Handle)
	{
		SDL_LockAudio();
		MusicPlaying=false;
		if(MusicFile)
			ModPlug_Unload(MusicFile);
		MusicFile=NULL;
		MusicLoaded=false;
		InMusic->Handle=NULL;
		SDL_UnlockAudio();
	}
	unguard;
}

void SDLCALL UNullAudioSubsystem::MusicPostMix(void* Userdata, Uint8* Stream, int Length)
{
	UNullAudioSubsystem* Audio=(UNullAudioSubsystem*)Userdata;
	if(!Audio || !Audio->MusicFile || !Audio->MusicPlaying || !Audio->MusicLoaded)
		return;

	INT Offset=0;
	while(Offset<Length && Audio->MusicRingCount>0)
	{
		INT Part=Min(Length-Offset,(INT)Audio->MusicRingCount);
		Part=Min(Part,(INT)sizeof(Audio->MusicRingBuffer)-Audio->MusicRingRead);
		if(Audio->MusicMixCalls<8)
		{
			UBOOL Active=false;
			for(INT i=0;i<Part;i+=32)
				Active|=Audio->MusicRingBuffer[Audio->MusicRingRead+i]!=0;
			Audio->MusicMixActiveBuffers+=Active!=false;
		}
		SDL_MixAudio(Stream+Offset,Audio->MusicRingBuffer+Audio->MusicRingRead,Part,SDL_MIX_MAXVOLUME);
		Audio->MusicRingRead=(Audio->MusicRingRead+Part)%(INT)sizeof(Audio->MusicRingBuffer);
		Audio->MusicRingCount-=Part;
		Offset+=Part;
	}
	Audio->MusicMixCalls++;
}

void UNullAudioSubsystem::FillMusicBuffer()
{
	if(!MusicFile || !MusicPlaying || !MusicLoaded || MusicMixErrors || MusicRingCount>=16384)
		return;
	// Decode one small block per game update. AHI only consumes ready PCM.
	const INT Part=(INT)sizeof(MusicMixBuffer);
	INT Read=ModPlug_Read(MusicFile,MusicMixBuffer,Part);
	if(Read<=0)
	{
		// This Amiga libmodplug build ignores mLoopCount for some IT/UMX
		// modules, so loop explicitly from the active UE1 song section.
		ModPlug_SeekOrder(MusicFile,MusicSection);
		Read=ModPlug_Read(MusicFile,MusicMixBuffer,Part);
		if(Read<=0)
		{
			MusicMixErrors++;
			MusicPlaying=false;
			return;
		}
	}
	SDL_LockAudio();
	INT First=Min(Read,(INT)sizeof(MusicRingBuffer)-MusicRingWrite);
	appMemcpy(MusicRingBuffer+MusicRingWrite,MusicMixBuffer,First);
	if(First<Read)
		appMemcpy(MusicRingBuffer,MusicMixBuffer+First,Read-First);
	MusicRingWrite=(MusicRingWrite+Read)%(INT)sizeof(MusicRingBuffer);
	MusicRingCount+=Read;
	SDL_UnlockAudio();
}

void UNullAudioSubsystem::StartMusic()
{
	if(!MusicFile || !MusicLoaded || MusicSection==255)
		return;
	SDL_LockAudio();
	ModPlug_SeekOrder(MusicFile,MusicSection);
	ModPlug_SetMasterVolume(MusicFile,Clamp((INT)MusicVolume*2,1,512));
	MusicRingRead=0;
	MusicRingWrite=0;
	MusicRingCount=0;
	MusicMixCalls=0;
	MusicMixActiveBuffers=0;
	MusicMixErrors=0;
	MusicMixReported=false;
	MusicPlaying=true;
	SDL_UnlockAudio();
	debugf(NAME_Init,"Music started: %s, section=%d, volume=%d",Music ? Music->GetName() : "None",(INT)MusicSection,(INT)MusicVolume);
}

void UNullAudioSubsystem::StopMusic()
{
	MusicPlaying=false;
}

FLOAT UNullAudioSubsystem::VoicePriority(const FVector& Location, FLOAT Volume, FLOAT Radius) const
{
	if(Radius>0.f && Viewport && Viewport->Actor)
		return Volume*(1.f-(Location-Viewport->Actor->Location).Size()/Radius);
	return Volume;
}

void UNullAudioSubsystem::StopVoice(INT Channel)
{
	if(Channel<0 || Channel>=MAX_MIX_CHANNELS)
		return;
	if(AudioOpen && Mix_Playing(Channel))
		Mix_HaltChannel(Channel);
	Voices[Channel].Actor=NULL;
	Voices[Channel].Sound=NULL;
	Voices[Channel].Id=0;
	Voices[Channel].Priority=0.f;
	Voices[Channel].Looping=false;
}

void UNullAudioSubsystem::UpdateVoice(INT Channel)
{
	FVoice& Voice=Voices[Channel];
	if(!Voice.Sound || !Voice.Id)
		return;
	FLOAT Distance=0.f;
	if(Voice.Radius>0.f)
		Distance=(Voice.Location-ListenerCoords.Origin).Size()/Voice.Radius;
	// Match NOpenALDrv's AL_LINEAR_DISTANCE_CLAMPED setup.  OpenAL keeps the
	// inner 10% of the sound radius at full volume, then applies a 1.1 rolloff
	// over the remaining radius.  The old 1-Distance curve left large UE1
	// ambient radii (fans, machinery, wind) much too loud across an entire room.
	const FLOAT ReferenceDistance=0.1f;
	const FLOAT Rolloff=1.1f;
	const FLOAT Attenuation = Distance<=ReferenceDistance
		? 1.f
		: Clamp(1.f-Rolloff*(Distance-ReferenceDistance)/(1.f-ReferenceDistance),0.f,1.f);
	const INT Volume=Clamp(appRound(Voice.Volume*Attenuation*(FLOAT)SoundVolume*128.f/255.f),0,128);
	Mix_Volume(Channel,Volume);
	Mix_SetPosition(Channel,0,(Uint8)Clamp(appRound(Distance*255.f),0,255));
	Voice.Priority=VoicePriority(Voice.Location,Voice.Volume,Voice.Radius);
}

UBOOL UNullAudioSubsystem::PlaySound(AActor* Actor, INT Id, USound* Sound, FVector Location, FLOAT Volume, FLOAT Radius, FLOAT Pitch)
{
	guard(UNullAudioSubsystem::PlaySound);
	if(!AudioOpen || !Viewport || !Sound)
		return false;
	if(!Sound->Handle)
		RegisterSound(Sound);
	if(!Sound->Handle)
		return false;
	if(SOUND_SLOT_IS(Id,SLOT_None))
		Id=16*--NextId;

	const FLOAT Priority=VoicePriority(Location,Volume,Radius);
	FVoice* Voice=NULL;
	FLOAT Lowest=Priority;
	for(INT i=0;i<MAX_MIX_CHANNELS;++i)
	{
		FVoice* Candidate=&Voices[i];
		if(Candidate->Id && ((Candidate->Id&~1)==(Id&~1)))
		{
			if(Id&1)
				return false;
			Voice=Candidate;
			break;
		}
		if(!Mix_Playing(i))
		{
			Voice=Candidate;
			break;
		}
		if(Candidate->Priority<=Lowest)
		{
			Lowest=Candidate->Priority;
			Voice=Candidate;
		}
	}
	if(!Voice)
		return false;
	const INT Channel=(INT)(Voice-Voices);
	StopVoice(Channel);
	Voice->Actor=Actor;
	Voice->Sound=Sound;
	Voice->Id=Id;
	Voice->Location=Location;
	Voice->Volume=Clamp(Volume,0.f,1.f);
	Voice->Radius=Radius;
	Voice->Pitch=Pitch;
	Voice->Priority=Priority;
	// UE1 marks genuinely looping WAVs with an smpl loop.  Do not force every
	// ambient slot to loop: NOpenALDrv also uses Sound->Looping alone, and some
	// ambient actors intentionally play a short one-shot sample (for example
	// machinery/fan transients).  Forcing those samples produced the rapid,
	// permanent restart heard on Amiga and kept mixer channels occupied.
	Voice->Looping=Sound->Looping;
	UpdateVoice(Channel);
	if(Mix_PlayChannel(Channel,(Mix_Chunk*)Sound->Handle,Voice->Looping ? -1 : 0)<0)
	{
		StopVoice(Channel);
		return false;
	}
	return true;
	unguard;
}

void UNullAudioSubsystem::NoteDestroy(AActor* Actor)
{
	guard(UNullAudioSubsystem::NoteDestroy);
	for(INT i=0;i<MAX_MIX_CHANNELS;++i)
	{
		if(Voices[i].Actor==Actor)
		{
			if(SOUND_SLOT_IS(Voices[i].Id,SLOT_Ambient))
				StopVoice(i);
			else
				Voices[i].Actor=NULL;
		}
	}
	unguard;
}

void UNullAudioSubsystem::Update(FPointRegion Region, FCoords& Listener)
{
	guard(UNullAudioSubsystem::Update);
	if(!Viewport || !Viewport->IsRealtime())
		return;
	ListenerCoords=Listener;

	if(Viewport->Actor && Viewport->Actor->XLevel)
	{
		for(INT i=0;i<Viewport->Actor->XLevel->Num();++i)
		{
			AActor* Actor=Viewport->Actor->XLevel->Actors(i);
			if(!Actor || !Actor->IsValid() || !Actor->AmbientSound)
				continue;
			if(FDistSquared(Viewport->Actor->Location,Actor->Location)>Square(Actor->WorldSoundRadius()))
				continue;
			const INT Id=AMBIENT_SOUND_ID(Actor->GetIndex());
			INT Channel=0;
			for(;Channel<MAX_MIX_CHANNELS;++Channel)
				if(Voices[Channel].Id==Id)
					break;
			if(Channel==MAX_MIX_CHANNELS)
				PlaySound(Actor,Id,Actor->AmbientSound,Actor->Location,AmbientFactor*Actor->SoundVolume/255.f,Actor->WorldSoundRadius(),Actor->SoundPitch/64.f);
		}
	}

	for(INT i=0;i<MAX_MIX_CHANNELS;++i)
	{
		FVoice& Voice=Voices[i];
		if(!Voice.Id || !Voice.Sound)
			continue;
		if(!Mix_Playing(i))
		{
			StopVoice(i);
			continue;
		}
		// Actors can disappear during map GC before the audio subsystem receives
		// NoteDestroy.  Do not keep probing or dereference that stale pointer on
		// every update; ambient voices belong to the actor and must stop, while a
		// one-shot sound may finish from its last known position.
		if(Voice.Actor && !Voice.Actor->IsValid())
		{
			Voice.Actor=NULL;
			if(SOUND_SLOT_IS(Voice.Id,SLOT_Ambient))
			{
				StopVoice(i);
				continue;
			}
		}
		if(Voice.Actor)
			Voice.Location=Voice.Actor->Location;
		if(SOUND_SLOT_IS(Voice.Id,SLOT_Ambient))
		{
			if(!Voice.Actor || Voice.Sound!=Voice.Actor->AmbientSound || FDistSquared(Viewport->Actor->Location,Voice.Actor->Location)>Square(Voice.Actor->WorldSoundRadius()))
			{
				StopVoice(i);
				continue;
			}
			Voice.Radius=Voice.Actor->WorldSoundRadius();
			Voice.Volume=AmbientFactor*Voice.Actor->SoundVolume/255.f;
		}
		UpdateVoice(i);
	}

	if(Viewport->Actor && Viewport->Actor->Transition!=MTRAN_None)
	{
		UMusic* NewMusic=Viewport->Actor->Song;
		const BYTE NewSection=Viewport->Actor->SongSection;
		if(Music && Music!=NewMusic)
			UnregisterMusic(Music);
		Music=NewMusic;
		MusicSection=NewSection;
		if(Music)
		{
			if(!Music->Handle)
				RegisterMusic(Music);
			if(MusicSection==255)
				StopMusic();
			else
				StartMusic();
		}
		else
			StopMusic();
		Viewport->Actor->Transition=MTRAN_None;
	}

	// Some UE1 maps arrive with Song already assigned but without a pending
	// transition.  Galaxy tolerated that state; start the current song too.
	if(Viewport->Actor && Viewport->Actor->Transition==MTRAN_None &&
		Viewport->Actor->Song && Viewport->Actor->SongSection!=255 &&
		(Music!=Viewport->Actor->Song || (!MusicPlaying && MusicMixErrors==0)))
	{
		if(Music && Music!=Viewport->Actor->Song)
			UnregisterMusic(Music);
		Music=Viewport->Actor->Song;
		MusicSection=Viewport->Actor->SongSection;
		if(!Music->Handle)
			RegisterMusic(Music);
		if(Music->Handle)
			StartMusic();
	}
	FillMusicBuffer();
	if(!MusicMixReported && MusicMixCalls>=8)
	{
		debugf(NAME_Init,"Music postmix: callbacks=%d, active-buffers=%d, errors=%d",MusicMixCalls,MusicMixActiveBuffers,MusicMixErrors);
		MusicMixReported=true;
	}
	unguard;
}

UBOOL UNullAudioSubsystem::GetLowQualitySetting()
{
	return LowSoundQuality;
}

IMPLEMENT_CLASS(UNullAudioSubsystem);
IMPLEMENT_PACKAGE(SoundDrv);
