#include "MTARadioSubsystem.h"
#include "MTAHeatSubsystem.h"
#include "MTAWeatherManager.h"
#include "Components/AudioComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"

namespace
{
	EMTARadioSegmentType TypeFromString(const FString& S)
	{
		if (S == TEXT("news")) return EMTARadioSegmentType::News;
		if (S == TEXT("traffic")) return EMTARadioSegmentType::Traffic;
		if (S == TEXT("weather")) return EMTARadioSegmentType::Weather;
		if (S == TEXT("ad")) return EMTARadioSegmentType::Ad;
		if (S == TEXT("jingle")) return EMTARadioSegmentType::Jingle;
		return EMTARadioSegmentType::Talk;
	}

	float EstimateSeconds(const FString& Text, float WordsPerSecond)
	{
		int32 Words = 1;
		for (const TCHAR C : Text)
		{
			if (C == TEXT(' '))
			{
				++Words;
			}
		}
		return Words / FMath::Max(0.5f, WordsPerSecond) + 1.0f;
	}

	bool IsNewsGroup(EMTARadioSegmentType T)
	{
		return T == EMTARadioSegmentType::News || T == EMTARadioSegmentType::Traffic || T == EMTARadioSegmentType::Weather;
	}
}

// ---------------------------------------------------------------------------------------------
void UMTARadioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Rng.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	LoadRadioData();
	LoadState();
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UMTARadioSubsystem::Tick), 0.1f);
}

void UMTARadioSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (MusicComp)
	{
		MusicComp->Stop();
	}
	Super::Deinitialize();
}

bool UMTARadioSubsystem::LoadRadioData(const FString& RelativePath)
{
	const TSharedPtr<FJsonObject> Root = MTAAudio::LoadJsonFile(RelativePath);
	if (!Root.IsValid())
	{
		return false;
	}
	Stations.Reset();
	if (const TSharedPtr<FJsonObject>* Set = nullptr; Root->TryGetObjectField(TEXT("settings"), Set))
	{
		AudioPath = MTAAudio::ReadString(*Set, TEXT("audioPath"), AudioPath);
		DuckAmount = MTAAudio::ReadNumber(*Set, TEXT("duckDuringSpeech"), DuckAmount);
		WordsPerSecond = MTAAudio::ReadNumber(*Set, TEXT("wordsPerSecond"), WordsPerSecond);
		JingleEvery = static_cast<int32>(MTAAudio::ReadNumber(*Set, TEXT("jingleEveryTracks"), 2));
		Volume = MTAAudio::ReadNumber(*Set, TEXT("defaultVolume"), Volume);
		LastStationFromSave = MTAAudio::ReadString(*Set, TEXT("defaultStation"));
	}
	const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
	if (!Root->TryGetArrayField(TEXT("stations"), Arr))
	{
		return false;
	}
	for (const TSharedPtr<FJsonValue>& SV : *Arr)
	{
		const TSharedPtr<FJsonObject> S = SV->AsObject();
		if (!S.IsValid())
		{
			continue;
		}
		FMTARadioStation St;
		St.Id = FName(*MTAAudio::ReadString(S, TEXT("id")));
		St.Name = MTAAudio::ReadString(S, TEXT("name"));
		St.Frequency = MTAAudio::ReadNumber(S, TEXT("frequency"), 100.f);
		St.Genre = MTAAudio::ReadString(S, TEXT("genre"));
		St.Tagline = MTAAudio::ReadString(S, TEXT("tagline"));

		const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
		if (S->TryGetArrayField(TEXT("hosts"), A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FMTARadioHost H;
				H.Id = FName(*MTAAudio::ReadString(O, TEXT("id")));
				H.Name = MTAAudio::ReadString(O, TEXT("name"));
				H.Bio = MTAAudio::ReadString(O, TEXT("bio"));
				St.Hosts.Add(H);
			}
		}
		if (S->TryGetArrayField(TEXT("jingles"), A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				St.Jingles.Add(V->AsString());
			}
		}
		if (S->TryGetArrayField(TEXT("tracks"), A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FMTARadioTrack T;
				T.Id = FName(*MTAAudio::ReadString(O, TEXT("id")));
				T.Title = MTAAudio::ReadString(O, TEXT("title"));
				T.Artist = MTAAudio::ReadString(O, TEXT("artist"));
				T.DurationSec = MTAAudio::ReadNumber(O, TEXT("durationSec"), 180.f);
				T.Bpm = MTAAudio::ReadNumber(O, TEXT("bpm"), 100.f);
				T.Key = MTAAudio::ReadString(O, TEXT("key"));
				T.Loop = FName(*MTAAudio::ReadString(O, TEXT("loop")));
				St.Tracks.Add(T);
			}
		}
		auto ReadScripts = [&](const TCHAR* Field, bool bAds, TArray<FMTARadioScript>& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* L = nullptr;
			if (!S->TryGetArrayField(Field, L))
			{
				return;
			}
			for (const TSharedPtr<FJsonValue>& V : *L)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FMTARadioScript Sc;
				Sc.Id = FName(*MTAAudio::ReadString(O, TEXT("id")));
				Sc.Type = bAds ? EMTARadioSegmentType::Ad : TypeFromString(MTAAudio::ReadString(O, TEXT("type")));
				Sc.HostId = FName(*MTAAudio::ReadString(O, TEXT("host")));
				Sc.Sponsor = MTAAudio::ReadString(O, TEXT("sponsor"));
				Sc.Text = MTAAudio::ReadString(O, TEXT("text"));
				Sc.CooldownMinutes = MTAAudio::ReadNumber(O, TEXT("cooldownMin"), bAds ? 8.f : 30.f);
				const TSharedPtr<FJsonObject>* When = nullptr;
				if (O->TryGetObjectField(TEXT("when"), When))
				{
					Sc.When = FMTAAudioCondition::FromJson(*When);
				}
				Out.Add(Sc);
			}
		};
		ReadScripts(TEXT("ads"), true, St.Ads);
		ReadScripts(TEXT("scripts"), false, St.Scripts);

		if (S->TryGetArrayField(TEXT("schedule"), A))
		{
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FMTARadioShow Sh;
				Sh.FromHour = MTAAudio::ReadNumber(O, TEXT("from"), 0.f);
				Sh.ToHour = MTAAudio::ReadNumber(O, TEXT("to"), 24.f);
				Sh.Name = MTAAudio::ReadString(O, TEXT("show"));
				Sh.HostId = FName(*MTAAudio::ReadString(O, TEXT("host")));
				const TSharedPtr<FJsonObject>* Mix = nullptr;
				if (O->TryGetObjectField(TEXT("mix"), Mix))
				{
					Sh.WeightMusic = MTAAudio::ReadNumber(*Mix, TEXT("music"), 0.f);
					Sh.WeightJingle = MTAAudio::ReadNumber(*Mix, TEXT("jingle"), 0.f);
					Sh.WeightAd = MTAAudio::ReadNumber(*Mix, TEXT("ad"), 0.f);
					Sh.WeightNews = MTAAudio::ReadNumber(*Mix, TEXT("news"), 0.f);
					Sh.WeightTalk = MTAAudio::ReadNumber(*Mix, TEXT("talk"), 0.f);
				}
				St.Schedule.Add(Sh);
			}
		}
		if (St.Tracks.Num() > 0)
		{
			Stations.Add(MoveTemp(St));
		}
	}
	return Stations.Num() > 0;
}

// ---------------------------------------------------------------------------------------------
FString UMTARadioSubsystem::StatePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("MTA6/radio_state.json");
}

void UMTARadioSubsystem::LoadState()
{
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *StatePath()))
	{
		return;
	}
	TSharedPtr<FJsonObject> Root;
	if (FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) && Root.IsValid())
	{
		LastStationFromSave = MTAAudio::ReadString(Root, TEXT("station"), LastStationFromSave);
		Volume = FMath::Clamp(MTAAudio::ReadNumber(Root, TEXT("volume"), Volume), 0.f, 1.f);
	}
}

void UMTARadioSubsystem::SaveState() const
{
	const FString Json = FString::Printf(TEXT("{\"station\":\"%s\",\"volume\":%.2f,\"on\":%s}"), *GetCurrentStationId().ToString().Replace(TEXT("\""), TEXT("")),
		Volume, bOn ? TEXT("true") : TEXT("false"));
	FFileHelper::SaveStringToFile(Json, *StatePath());
}

// ---------------------------------------------------------------------------------------------
FName UMTARadioSubsystem::GetCurrentStationId() const
{
	return Stations.IsValidIndex(CurrentStation) ? Stations[CurrentStation].Id : FName(*LastStationFromSave);
}

bool UMTARadioSubsystem::GetCurrentStation(FMTARadioStation& Out) const
{
	if (!Stations.IsValidIndex(CurrentStation))
	{
		return false;
	}
	Out = Stations[CurrentStation];
	return true;
}

bool UMTARadioSubsystem::GetCurrentTrack(FMTARadioTrack& Out) const
{
	if (Stations.IsValidIndex(CurrentStation) && Stations[CurrentStation].Tracks.IsValidIndex(CurrentTrack))
	{
		Out = Stations[CurrentStation].Tracks[CurrentTrack];
		return true;
	}
	return false;
}

const FMTARadioShow* UMTARadioSubsystem::FindShow(const FMTARadioStation& St) const
{
	for (const FMTARadioShow& Sh : St.Schedule)
	{
		if (MTAAudio::HourInRange(Context.Hour, Sh.FromHour, Sh.ToHour))
		{
			return &Sh;
		}
	}
	return St.Schedule.Num() > 0 ? &St.Schedule[0] : nullptr;
}

bool UMTARadioSubsystem::GetCurrentShow(FMTARadioShow& Out) const
{
	if (!Stations.IsValidIndex(CurrentStation))
	{
		return false;
	}
	if (const FMTARadioShow* Sh = FindShow(Stations[CurrentStation]))
	{
		Out = *Sh;
		return true;
	}
	return false;
}

FString UMTARadioSubsystem::HostName(const FMTARadioStation& St, FName HostId) const
{
	for (const FMTARadioHost& H : St.Hosts)
	{
		if (H.Id == HostId)
		{
			return H.Name;
		}
	}
	return St.Hosts.Num() > 0 ? St.Hosts[0].Name : St.Name;
}

// ---------------------------------------------------------------------------------------------
bool UMTARadioSubsystem::SetStation(FName StationId)
{
	const int32 Idx = Stations.IndexOfByPredicate([&](const FMTARadioStation& S) { return S.Id == StationId; });
	if (Idx == INDEX_NONE)
	{
		return false;
	}
	CurrentStation = Idx;
	bOn = true;
	Pending.Reset();
	SpeechRemaining = 0.f;
	LastTrack = INDEX_NONE;
	const FMTARadioStation& St = Stations[Idx];
	const int32 T = Rng.RandRange(0, St.Tracks.Num() - 1);
	// "Live"-Gefuehl: man steigt mitten im Stueck ein.
	StartTrack(T, Rng.FRandRange(0.f, St.Tracks[T].DurationSec * 0.7f));
	if (St.Jingles.Num() > 0 && Rng.FRand() < 0.6f)
	{
		FMTARadioSegment J;
		J.Type = EMTARadioSegmentType::Jingle;
		J.StationId = St.Id;
		J.Speaker = St.Name;
		J.Text = St.Jingles[Rng.RandRange(0, St.Jingles.Num() - 1)];
		J.DurationSec = EstimateSeconds(J.Text, WordsPerSecond);
		EnqueueSegment(J);
	}
	OnStationChanged.Broadcast(St.Id);
	SaveState();
	return true;
}

void UMTARadioSubsystem::NextStation()
{
	if (Stations.Num() == 0) return;
	SetStation(Stations[(FMath::Max(CurrentStation, -1) + 1) % Stations.Num()].Id);
}

void UMTARadioSubsystem::PreviousStation()
{
	if (Stations.Num() == 0) return;
	const int32 I = CurrentStation <= 0 ? Stations.Num() - 1 : CurrentStation - 1;
	SetStation(Stations[I].Id);
}

void UMTARadioSubsystem::TurnOff()
{
	bOn = false;
	Pending.Reset();
	SpeechRemaining = 0.f;
	if (MusicComp)
	{
		MusicComp->Stop();
	}
	SaveState();
}

void UMTARadioSubsystem::TurnOn()
{
	if (bOn || Stations.Num() == 0)
	{
		return;
	}
	const FName Last(*LastStationFromSave);
	if (!SetStation(Last))
	{
		SetStation(Stations[0].Id);
	}
}

void UMTARadioSubsystem::SetVolume(float NewVolume)
{
	Volume = FMath::Clamp(NewVolume, 0.f, 1.f);
	SaveState();
	ApplyAudio();
}

// ---------------------------------------------------------------------------------------------
USoundBase* UMTARadioSubsystem::LoadLoop(FName Loop)
{
	if (const TObjectPtr<USoundBase>* C = SoundCache.Find(Loop))
	{
		return *C;
	}
	const FString Name = Loop.ToString();
	const FSoftObjectPath Path(AudioPath + Name + TEXT(".") + Name);
	USoundBase* S = Cast<USoundBase>(Path.TryLoad());
	if (S)
	{
		if (USoundWave* W = Cast<USoundWave>(S))
		{
			W->bLooping = true; // Loops sind nahtlos synthetisiert
		}
		SoundCache.Add(Loop, S);
	}
	else if (!bWarnedMissing)
	{
		bWarnedMissing = true;
		UE_LOG(LogTemp, Log, TEXT("MTARadio: Audio-Asset %s nicht importiert (Content/Audio/Generated/*.wav). Radio laeuft ohne Ton weiter."), *Path.ToString());
	}
	return S;
}

void UMTARadioSubsystem::StartTrack(int32 TrackIndex, float ElapsedOffset)
{
	if (!Stations.IsValidIndex(CurrentStation) || !Stations[CurrentStation].Tracks.IsValidIndex(TrackIndex))
	{
		return;
	}
	LastTrack = CurrentTrack;
	CurrentTrack = TrackIndex;
	TrackElapsed = ElapsedOffset;
	const FMTARadioTrack& Tr = Stations[CurrentStation].Tracks[TrackIndex];

	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (USoundBase* Sound = LoadLoop(Tr.Loop); Sound && World)
	{
		const float LoopLen = Sound->GetDuration();
		const float Start = (LoopLen > 1.f && LoopLen < 1000.f) ? FMath::Fmod(ElapsedOffset, LoopLen) : 0.f;
		if (!MusicComp)
		{
			MusicComp = UGameplayStatics::SpawnSound2D(World, Sound, Volume * DuckCurrent, 1.f, Start, nullptr, true, false);
		}
		else
		{
			MusicComp->Stop();
			MusicComp->SetSound(Sound);
			MusicComp->SetVolumeMultiplier(Volume * DuckCurrent);
			MusicComp->Play(Start);
		}
	}
	OnTrackChanged.Broadcast(Tr);
}

void UMTARadioSubsystem::AdvanceProgram()
{
	const FMTARadioStation& St = Stations[CurrentStation];
	int32 Next = Rng.RandRange(0, St.Tracks.Num() - 1);
	if (St.Tracks.Num() > 1)
	{
		for (int32 Guard = 0; Guard < 6 && Next == CurrentTrack; ++Guard)
		{
			Next = Rng.RandRange(0, St.Tracks.Num() - 1);
		}
	}
	StartTrack(Next, 0.f);
	++TracksSinceJingle;
	QueueBreakAfterTrack();
}

bool UMTARadioSubsystem::PickScript(const FMTARadioStation& St, EMTARadioSegmentType Kind, FMTARadioSegment& Out)
{
	const TArray<FMTARadioScript>& Pool = Kind == EMTARadioSegmentType::Ad ? St.Ads : St.Scripts;
	TArray<const FMTARadioScript*> Cand;
	TArray<float> Weights;
	float Sum = 0.f;
	for (const FMTARadioScript& Sc : Pool)
	{
		const bool bTypeOk = Kind == EMTARadioSegmentType::Ad ? true : (Kind == EMTARadioSegmentType::News ? IsNewsGroup(Sc.Type) : Sc.Type == Kind);
		if (!bTypeOk || !Sc.When.Evaluate(Context))
		{
			continue;
		}
		const double* Last = LastUsed.Find(FName(*(St.Id.ToString() + TEXT("/") + Sc.Id.ToString())));
		if (Last && Clock - *Last < Sc.CooldownMinutes * 60.0)
		{
			continue;
		}
		// Reaktive Bausteine (Bedingungen gesetzt) werden bevorzugt.
		const bool bReactive = Sc.When.WantedMin > 0 || Sc.When.Weather.Num() > 0 || Sc.When.FlagsAll.Num() > 0 || Sc.When.District.Num() > 0;
		Cand.Add(&Sc);
		const float W = bReactive ? 3.f : 1.f;
		Weights.Add(W);
		Sum += W;
	}
	if (Cand.Num() == 0)
	{
		return false;
	}
	float R = Rng.FRand() * Sum;
	int32 Pick = Cand.Num() - 1;
	for (int32 I = 0; I < Cand.Num(); ++I)
	{
		R -= Weights[I];
		if (R <= 0.f)
		{
			Pick = I;
			break;
		}
	}
	const FMTARadioScript& Sc = *Cand[Pick];
	LastUsed.Add(FName(*(St.Id.ToString() + TEXT("/") + Sc.Id.ToString())), Clock);
	Out.Type = Sc.Type;
	Out.StationId = St.Id;
	Out.ScriptId = Sc.Id;
	const FMTARadioShow* Show = FindShow(St);
	Out.Speaker = Sc.Type == EMTARadioSegmentType::Ad ? Sc.Sponsor : HostName(St, Sc.HostId.IsNone() && Show ? Show->HostId : Sc.HostId);
	Out.Text = MTAAudio::FormatText(Sc.Text, Context);
	Out.DurationSec = EstimateSeconds(Out.Text, WordsPerSecond);
	return true;
}

void UMTARadioSubsystem::QueueBreakAfterTrack()
{
	const FMTARadioStation& St = Stations[CurrentStation];
	if (TracksSinceJingle >= JingleEvery && St.Jingles.Num() > 0)
	{
		TracksSinceJingle = 0;
		FMTARadioSegment J;
		J.Type = EMTARadioSegmentType::Jingle;
		J.StationId = St.Id;
		J.Speaker = St.Name;
		J.Text = St.Jingles[Rng.RandRange(0, St.Jingles.Num() - 1)];
		J.DurationSec = EstimateSeconds(J.Text, WordsPerSecond);
		EnqueueSegment(J);
	}
	const FMTARadioShow* Sh = FindShow(St);
	if (!Sh)
	{
		return;
	}
	const float Total = Sh->WeightMusic + Sh->WeightAd + Sh->WeightNews + Sh->WeightTalk;
	if (Total <= 0.f || Rng.FRand() > 1.f - Sh->WeightMusic / Total)
	{
		return; // diesmal nur Musik
	}
	float R = Rng.FRand() * (Sh->WeightAd + Sh->WeightNews + Sh->WeightTalk);
	EMTARadioSegmentType Kind = EMTARadioSegmentType::Talk;
	if ((R -= Sh->WeightAd) <= 0.f) Kind = EMTARadioSegmentType::Ad;
	else if ((R -= Sh->WeightNews) <= 0.f) Kind = EMTARadioSegmentType::News;

	FMTARadioSegment Seg;
	if (PickScript(St, Kind, Seg))
	{
		EnqueueSegment(Seg);
		if (Kind == EMTARadioSegmentType::Ad && Rng.FRand() < 0.5f && PickScript(St, Kind, Seg))
		{
			EnqueueSegment(Seg);
		}
	}
}

void UMTARadioSubsystem::EnqueueSegment(const FMTARadioSegment& Seg, bool bFront)
{
	if (bFront)
	{
		Pending.Insert(Seg, 0);
	}
	else
	{
		Pending.Add(Seg);
	}
}

void UMTARadioSubsystem::StartNextSegment()
{
	if (Pending.Num() == 0)
	{
		return;
	}
	ActiveSegment = Pending[0];
	Pending.RemoveAt(0);
	SpeechRemaining = ActiveSegment.DurationSec;
	OnSegmentStarted.Broadcast(ActiveSegment);
}

void UMTARadioSubsystem::InjectLine(const FString& Speaker, const FString& Text, EMTARadioSegmentType Type)
{
	if (!bOn)
	{
		return;
	}
	FMTARadioSegment Seg;
	Seg.Type = Type;
	Seg.StationId = GetCurrentStationId();
	Seg.Speaker = Speaker;
	Seg.Text = MTAAudio::FormatText(Text, Context);
	Seg.DurationSec = EstimateSeconds(Seg.Text, WordsPerSecond);
	EnqueueSegment(Seg, true);
}

// ---------------------------------------------------------------------------------------------
void UMTARadioSubsystem::TryReactive(bool bWantedRise, FName NewFlag)
{
	if (!bOn || !Stations.IsValidIndex(CurrentStation))
	{
		return;
	}
	const FMTARadioStation& St = Stations[CurrentStation];
	for (const FMTARadioScript& Sc : St.Scripts)
	{
		const bool bMatch = (bWantedRise && Sc.When.WantedMin > 0) || (!NewFlag.IsNone() && Sc.When.FlagsAll.Contains(NewFlag));
		if (!bMatch || !Sc.When.Evaluate(Context))
		{
			continue;
		}
		const FName Key(*(St.Id.ToString() + TEXT("/") + Sc.Id.ToString()));
		const double* Last = LastUsed.Find(Key);
		if (Last && Clock - *Last < Sc.CooldownMinutes * 60.0)
		{
			continue;
		}
		LastUsed.Add(Key, Clock);
		FMTARadioSegment Seg;
		Seg.Type = Sc.Type;
		Seg.StationId = St.Id;
		Seg.ScriptId = Sc.Id;
		Seg.Speaker = HostName(St, Sc.HostId);
		Seg.Text = MTAAudio::FormatText(Sc.Text, Context);
		Seg.DurationSec = EstimateSeconds(Seg.Text, WordsPerSecond);
		EnqueueSegment(Seg, true);
		return; // pro Ausloeser hoechstens eine Zeile
	}
}

void UMTARadioSubsystem::SetContext(const FMTAAudioContext& NewContext)
{
	const int32 OldWanted = Context.WantedLevel;
	const TSet<FName> OldFlags = Context.Flags;
	Context = NewContext;
	if (Context.WantedLevel > OldWanted)
	{
		TryReactive(true, NAME_None);
	}
	for (const FName& F : Context.Flags)
	{
		if (!OldFlags.Contains(F))
		{
			TryReactive(false, F);
		}
	}
}

void UMTARadioSubsystem::SetWantedLevel(int32 Level)
{
	Level = FMath::Clamp(Level, 0, 5);
	const bool bRise = Level > Context.WantedLevel;
	Context.WantedLevel = Level;
	if (bRise)
	{
		TryReactive(true, NAME_None);
	}
}

void UMTARadioSubsystem::SetDistrict(FName DistrictId)
{
	Context.District = DistrictId;
}

void UMTARadioSubsystem::SetGameHour(float Hour)
{
	Context.Hour = FMath::Fmod(FMath::Max(0.f, Hour), 24.f);
}

void UMTARadioSubsystem::SetStoryFlag(FName Flag, bool bSet)
{
	if (bSet)
	{
		const bool bNew = !Context.Flags.Contains(Flag);
		Context.Flags.Add(Flag);
		if (bNew)
		{
			TryReactive(false, Flag);
		}
	}
	else
	{
		Context.Flags.Remove(Flag);
	}
}

void UMTARadioSubsystem::RefreshContextFromWorld()
{
	UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
	if (!World)
	{
		return;
	}
	if (const AMTAWeatherManager* WM = AMTAWeatherManager::Find(World))
	{
		const FMTAWeatherParams& P = WM->GetParams();
		Context.Weather = MTAWeather::StateName(P.TransitionAlpha > 0.5f ? P.TargetState : P.State);
	}
	if (const UMTAHeatSubsystem* Heat = World->GetSubsystem<UMTAHeatSubsystem>())
	{
		SetWantedLevel(FMath::Clamp(FMath::RoundToInt(Heat->GetHeat(Context.District)), 0, 5));
	}
}

// ---------------------------------------------------------------------------------------------
void UMTARadioSubsystem::ApplyAudio()
{
	if (MusicComp && bOn)
	{
		MusicComp->SetVolumeMultiplier(Volume * DuckCurrent);
	}
}

bool UMTARadioSubsystem::Tick(float Dt)
{
	Clock += Dt;
	if (bAutoReadWorld)
	{
		WorldReadTimer += Dt;
		if (WorldReadTimer >= 1.f)
		{
			WorldReadTimer = 0.f;
			RefreshContextFromWorld();
		}
	}
	if (!bOn || !Stations.IsValidIndex(CurrentStation))
	{
		return true;
	}

	TrackElapsed += Dt;
	if (SpeechRemaining > 0.f)
	{
		SpeechRemaining -= Dt;
	}
	if (SpeechRemaining <= 0.f && Pending.Num() > 0)
	{
		StartNextSegment();
	}
	DuckCurrent = FMath::FInterpTo(DuckCurrent, SpeechRemaining > 0.f ? DuckAmount : 1.f, Dt, 4.f);

	const FMTARadioStation& St = Stations[CurrentStation];
	if (St.Tracks.IsValidIndex(CurrentTrack) && TrackElapsed >= St.Tracks[CurrentTrack].DurationSec)
	{
		AdvanceProgram();
	}
	ApplyAudio();
	return true;
}
