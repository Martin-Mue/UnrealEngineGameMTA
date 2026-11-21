#include "MTAAudioAmbienceSubsystem.h"
#include "MTAWeatherManager.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundWave.h"
#include "TimerManager.h"

void UMTAAmbienceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Rng.Initialize(static_cast<int32>(FPlatformTime::Cycles()));
	LoadAmbienceData();
	ManagerReadyHandle = AMTAWeatherManager::OnManagerReady().AddWeakLambda(this, [this](AMTAWeatherManager* M)
	{
		if (M && M->GetWorld() == GetWorld())
		{
			BoundManager = M;
			M->RegisterConsumer(this);
		}
	});
	BindWeather();
}

void UMTAAmbienceSubsystem::Deinitialize()
{
	AMTAWeatherManager::OnManagerReady().Remove(ManagerReadyHandle);
	if (AMTAWeatherManager* M = BoundManager.Get())
	{
		M->UnregisterConsumer(this);
	}
	for (auto& P : LayerComps)
	{
		if (P.Value)
		{
			P.Value->Stop();
		}
	}
	LayerComps.Reset();
	Super::Deinitialize();
}

void UMTAAmbienceSubsystem::BindWeather()
{
	if (BoundManager.IsValid())
	{
		return;
	}
	if (AMTAWeatherManager* M = AMTAWeatherManager::Find(this))
	{
		BoundManager = M;
		M->RegisterConsumer(this);
	}
}

void UMTAAmbienceSubsystem::OnWeatherUpdated_Implementation(const FMTAWeatherParams& Params)
{
	Weather = Params;
	Context.Weather = MTAWeather::StateName(Params.TransitionAlpha > 0.5f ? Params.TargetState : Params.State);
}

// ---------------------------------------------------------------------------------------------
bool UMTAAmbienceSubsystem::LoadAmbienceData(const FString& RelativePath)
{
	const TSharedPtr<FJsonObject> Root = MTAAudio::LoadJsonFile(RelativePath);
	if (!Root.IsValid())
	{
		return false;
	}
	AudioPath = MTAAudio::ReadString(Root, TEXT("audioPath"), AudioPath);
	Smoothing = MTAAudio::ReadNumber(Root, TEXT("smoothingSeconds"), 3.f);

	Layers.Reset();
	if (const TSharedPtr<FJsonObject>* L = nullptr; Root->TryGetObjectField(TEXT("layers"), L))
	{
		for (const auto& P : (*L)->Values)
		{
			const TSharedPtr<FJsonObject> O = P.Value->AsObject();
			FLayerDef D;
			D.File = MTAAudio::ReadString(O, TEXT("file"));
			D.bLoop = O->HasField(TEXT("loop")) ? O->GetBoolField(TEXT("loop")) : true;
			D.BaseGain = FMath::Pow(10.f, MTAAudio::ReadNumber(O, TEXT("baseGainDb"), 0.f) / 20.f);
			D.bDamped = O->HasField(TEXT("dampedByWeather")) ? O->GetBoolField(TEXT("dampedByWeather")) : true;
			Layers.Add(FName(*P.Key), D);
		}
	}

	static const TCHAR* BandNames[4] = { TEXT("dawn"), TEXT("day"), TEXT("dusk"), TEXT("night") };
	if (const TSharedPtr<FJsonObject>* TB = nullptr; Root->TryGetObjectField(TEXT("timeBands"), TB))
	{
		for (int32 I = 0; I < 4; ++I)
		{
			if (const TSharedPtr<FJsonObject>* B = nullptr; (*TB)->TryGetObjectField(BandNames[I], B))
			{
				BandFrom[I] = MTAAudio::ReadNumber(*B, TEXT("from"), BandFrom[I]);
				BandTo[I] = MTAAudio::ReadNumber(*B, TEXT("to"), BandTo[I]);
			}
		}
	}

	DistrictBands.Reset();
	if (const TSharedPtr<FJsonObject>* Ds = nullptr; Root->TryGetObjectField(TEXT("districts"), Ds))
	{
		for (const auto& P : (*Ds)->Values)
		{
			const TSharedPtr<FJsonObject> D = P.Value->AsObject();
			TArray<TMap<FName, float>> Bands;
			Bands.SetNum(4);
			for (int32 I = 0; I < 4; ++I)
			{
				if (const TSharedPtr<FJsonObject>* B = nullptr; D->TryGetObjectField(BandNames[I], B))
				{
					for (const auto& LP : (*B)->Values)
					{
						Bands[I].Add(FName(*LP.Key), static_cast<float>(LP.Value->AsNumber()));
					}
				}
			}
			DistrictBands.Add(FName(*P.Key), MoveTemp(Bands));
		}
	}

	WeatherMods.Reset();
	if (const TSharedPtr<FJsonObject>* Wm = nullptr; Root->TryGetObjectField(TEXT("weatherMods"), Wm))
	{
		for (const auto& P : (*Wm)->Values)
		{
			TMap<FName, FMod> Mods;
			if (const TSharedPtr<FJsonObject> O = P.Value->AsObject(); O.IsValid())
			{
				for (const auto& LP : O->Values)
				{
					const TSharedPtr<FJsonObject> M = LP.Value->AsObject();
					FMod Mod;
					Mod.Mul = MTAAudio::ReadNumber(M, TEXT("mul"), 1.f);
					Mod.Add = MTAAudio::ReadNumber(M, TEXT("add"), 0.f);
					Mods.Add(FName(*LP.Key), Mod);
				}
			}
			WeatherMods.Add(FName(*P.Key), MoveTemp(Mods));
		}
	}

	ReverbTable.Reset();
	if (const TSharedPtr<FJsonObject>* Rv = nullptr; Root->TryGetObjectField(TEXT("reverb"), Rv))
	{
		auto ReadParams = [](const TSharedPtr<FJsonObject>& O) {
			FMTAReverbParams R;
			R.DecaySeconds = MTAAudio::ReadNumber(O, TEXT("decaySeconds"), R.DecaySeconds);
			R.Wet = MTAAudio::ReadNumber(O, TEXT("wet"), R.Wet);
			R.PreDelayMs = MTAAudio::ReadNumber(O, TEXT("preDelayMs"), R.PreDelayMs);
			R.ReflectionGain = MTAAudio::ReadNumber(O, TEXT("reflectionGain"), R.ReflectionGain);
			R.HighCutHz = MTAAudio::ReadNumber(O, TEXT("highCutHz"), R.HighCutHz);
			return R;
		};
		const TArray<TSharedPtr<FJsonValue>>* T = nullptr;
		if ((*Rv)->TryGetArrayField(TEXT("byStreetWidth"), T))
		{
			for (const TSharedPtr<FJsonValue>& V : *T)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				const FMTAReverbParams R = ReadParams(O);
				ReverbTable.Add({ MTAAudio::ReadNumber(O, TEXT("widthM"), 10.f), R.DecaySeconds, R.Wet, R.PreDelayMs, R.ReflectionGain, R.HighCutHz });
			}
			ReverbTable.Sort([](const FReverbPoint& A, const FReverbPoint& B) { return A.Width < B.Width; });
		}
		if (const TSharedPtr<FJsonObject>* O = nullptr; (*Rv)->TryGetObjectField(TEXT("tunnel"), O)) TunnelReverb = ReadParams(*O);
		if (const TSharedPtr<FJsonObject>* O = nullptr; (*Rv)->TryGetObjectField(TEXT("openField"), O)) OpenReverb = ReadParams(*O);
		DistrictWetMul.Reset();
		if (const TSharedPtr<FJsonObject>* Off = nullptr; (*Rv)->TryGetObjectField(TEXT("districtOffset"), Off))
		{
			for (const auto& P : (*Off)->Values)
			{
				DistrictWetMul.Add(FName(*P.Key), MTAAudio::ReadNumber(P.Value->AsObject(), TEXT("wetMul"), 1.f));
			}
		}
	}

	AegisLines.Reset();
	PoliceLines.Reset();
	AegisDensity.Reset();
	if (const TSharedPtr<FJsonObject>* Ae = nullptr; Root->TryGetObjectField(TEXT("aegis"), Ae))
	{
		AegisMinInterval = MTAAudio::ReadNumber(*Ae, TEXT("minIntervalSeconds"), 45.f);
		AegisMaxInterval = MTAAudio::ReadNumber(*Ae, TEXT("maxIntervalSeconds"), 140.f);
		PoliceInterval = MTAAudio::ReadNumber(*Ae, TEXT("policeIntervalSeconds"), 12.f);
		if (const TSharedPtr<FJsonObject>* Dn = nullptr; (*Ae)->TryGetObjectField(TEXT("aegisDensity"), Dn))
		{
			for (const auto& P : (*Dn)->Values)
			{
				AegisDensity.Add(FName(*P.Key), static_cast<float>(P.Value->AsNumber()));
			}
		}
		auto ReadLines = [](const TSharedPtr<FJsonObject>& Parent, const TCHAR* Field, TArray<FAegisEntry>& Out) {
			const TArray<TSharedPtr<FJsonValue>>* A = nullptr;
			if (!Parent->TryGetArrayField(Field, A)) return;
			for (const TSharedPtr<FJsonValue>& V : *A)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				FAegisEntry E;
				E.Id = FName(*MTAAudio::ReadString(O, TEXT("id")));
				E.Speaker = MTAAudio::ReadString(O, TEXT("speaker"));
				E.Category = FName(*MTAAudio::ReadString(O, TEXT("category"), TEXT("Announcement")));
				E.Text = MTAAudio::ReadString(O, TEXT("text"));
				if (const TSharedPtr<FJsonObject>* W = nullptr; O->TryGetObjectField(TEXT("when"), W)) E.When = FMTAAudioCondition::FromJson(*W);
				Out.Add(E);
			}
		};
		ReadLines(*Ae, TEXT("announcements"), AegisLines);
		ReadLines(*Ae, TEXT("police"), PoliceLines);
	}
	bLoaded = Layers.Num() > 0 && DistrictBands.Num() > 0;
	return bLoaded;
}

// ---------------------------------------------------------------------------------------------
void UMTAAmbienceSubsystem::SetListenerContext(EMTADistrict District, float GameHour, float StreetWidthMeters, bool bInTunnel)
{
	SetDistrict(District);
	SetGameHour(GameHour);
	SetStreetWidth(StreetWidthMeters);
	SetInTunnel(bInTunnel);
}

void UMTAAmbienceSubsystem::SetDistrict(EMTADistrict District)
{
	Context.District = MTAAudio::DistrictId(District);
	const float* D = AegisDensity.Find(Context.District);
	Context.AegisDensity = D ? *D : 0.5f;
}

void UMTAAmbienceSubsystem::SetGameHour(float Hour) { Context.Hour = FMath::Fmod(FMath::Max(0.f, Hour), 24.f); }
void UMTAAmbienceSubsystem::SetStreetWidth(float Meters) { StreetWidth = Meters; }
void UMTAAmbienceSubsystem::SetInTunnel(bool bInTunnel) { bTunnel = bInTunnel; }
void UMTAAmbienceSubsystem::SetWantedLevel(int32 Level) { Context.WantedLevel = FMath::Clamp(Level, 0, 5); }

void UMTAAmbienceSubsystem::SetStoryFlag(FName Flag, bool bSet)
{
	if (bSet) Context.Flags.Add(Flag); else Context.Flags.Remove(Flag);
}

float UMTAAmbienceSubsystem::GetLayerGain(FName Layer) const
{
	const float* G = Gains.Find(Layer);
	return G ? *G : 0.f;
}

// ---------------------------------------------------------------------------------------------
void UMTAAmbienceSubsystem::ComputeBandWeights(float Hour, float Out[4]) const
{
	// Innerhalb des Bandes 1, ausserhalb linearer Abfall ueber 1 Stunde (Crossfade an den Grenzen).
	float Sum = 0.f;
	for (int32 I = 0; I < 4; ++I)
	{
		float Dist = 0.f;
		if (!MTAAudio::HourInRange(Hour, BandFrom[I], BandTo[I]))
		{
			auto Circ = [](float A, float B) { const float D = FMath::Abs(A - B); return FMath::Min(D, 24.f - D); };
			Dist = FMath::Min(Circ(Hour, BandFrom[I]), Circ(Hour, BandTo[I]));
		}
		Out[I] = FMath::Clamp(1.f - Dist, 0.f, 1.f);
		Sum += Out[I];
	}
	for (int32 I = 0; I < 4; ++I)
	{
		Out[I] = Sum > KINDA_SMALL_NUMBER ? Out[I] / Sum : (I == 1 ? 1.f : 0.f);
	}
}

void UMTAAmbienceSubsystem::ComputeGainsForState(EMTAWeatherState State, const float Band[4], TMap<FName, float>& Out) const
{
	Out.Reset();
	const TArray<TMap<FName, float>>* Bands = DistrictBands.Find(Context.District);
	if (Bands)
	{
		for (int32 I = 0; I < 4; ++I)
		{
			for (const auto& P : (*Bands)[I])
			{
				Out.FindOrAdd(P.Key) += P.Value * Band[I];
			}
		}
	}
	const TMap<FName, FMod>* Mods = WeatherMods.Find(MTAWeather::StateName(State));
	if (Mods)
	{
		for (const auto& M : *Mods)
		{
			float& G = Out.FindOrAdd(M.Key);
			G = G * M.Value.Mul + M.Value.Add;
		}
	}
}

void UMTAAmbienceSubsystem::ComputeTargetGains(TMap<FName, float>& Out) const
{
	float Band[4];
	ComputeBandWeights(Context.Hour, Band);
	TMap<FName, float> A, B;
	ComputeGainsForState(Weather.State, Band, A);
	ComputeGainsForState(Weather.TargetState, Band, B);
	const float Alpha = Weather.TransitionAlpha;
	Out.Reset();
	for (const auto& P : Layers)
	{
		const float GA = A.FindRef(P.Key);
		const float GB = B.FindRef(P.Key);
		float G = FMath::Lerp(GA, GB, Alpha);
		if (P.Value.bDamped)
		{
			G *= 1.f - Weather.SoundDamping;
		}
		if (bTunnel && P.Key == TEXT("wind"))
		{
			G *= 0.2f;
		}
		Out.Add(P.Key, FMath::Clamp(G, 0.f, 1.f));
	}
}

FMTAReverbParams UMTAAmbienceSubsystem::ComputeReverbTarget() const
{
	FMTAReverbParams R;
	if (bTunnel)
	{
		R = TunnelReverb;
	}
	else if (StreetWidth <= 0.f || ReverbTable.Num() == 0)
	{
		R = OpenReverb;
	}
	else
	{
		int32 Hi = 0;
		while (Hi < ReverbTable.Num() && ReverbTable[Hi].Width < StreetWidth) ++Hi;
		auto Make = [](const FReverbPoint& P) { FMTAReverbParams X; X.DecaySeconds = P.Decay; X.Wet = P.Wet; X.PreDelayMs = P.PreDelay; X.ReflectionGain = P.Refl; X.HighCutHz = P.HighCut; return X; };
		if (Hi == 0) R = Make(ReverbTable[0]);
		else if (Hi >= ReverbTable.Num()) R = Make(ReverbTable.Last());
		else
		{
			const FReverbPoint& A = ReverbTable[Hi - 1];
			const FReverbPoint& B = ReverbTable[Hi];
			const float T = (StreetWidth - A.Width) / FMath::Max(0.01f, B.Width - A.Width);
			R.DecaySeconds = FMath::Lerp(A.Decay, B.Decay, T);
			R.Wet = FMath::Lerp(A.Wet, B.Wet, T);
			R.PreDelayMs = FMath::Lerp(A.PreDelay, B.PreDelay, T);
			R.ReflectionGain = FMath::Lerp(A.Refl, B.Refl, T);
			R.HighCutHz = FMath::Lerp(A.HighCut, B.HighCut, T);
		}
		R.Wet *= DistrictWetMul.FindRef(Context.District) > 0.f ? DistrictWetMul.FindRef(Context.District) : 1.f;
	}
	// Nebel/Regen daempfen Hoehen und verlaengern den Eindruck minimal.
	R.HighCutHz *= 1.f - 0.3f * Weather.SoundDamping;
	return R;
}

// ---------------------------------------------------------------------------------------------
void UMTAAmbienceSubsystem::FireLine(const FAegisEntry& E)
{
	FMTAAegisLine L;
	L.Id = E.Id;
	L.Speaker = E.Speaker;
	L.Category = E.Category;
	L.Text = MTAAudio::FormatText(E.Text, Context);
	L.DurationSec = FMath::Max(2.f, L.Text.Len() / 14.f);
	OnAegisLine.Broadcast(L);
}

void UMTAAmbienceSubsystem::TickAegis(float Dt)
{
	AegisTimer -= Dt;
	if (AegisTimer <= 0.f)
	{
		const float Density = FMath::Max(0.1f, Context.AegisDensity);
		AegisTimer = Rng.FRandRange(AegisMinInterval, AegisMaxInterval) / Density;
		TArray<const FAegisEntry*> C;
		for (const FAegisEntry& E : AegisLines)
		{
			if (E.When.Evaluate(Context)) C.Add(&E);
		}
		if (C.Num() > 0 && Context.AegisDensity > 0.15f)
		{
			FireLine(*C[Rng.RandRange(0, C.Num() - 1)]);
		}
	}
	if (Context.WantedLevel > 0)
	{
		PoliceTimer -= Dt;
		if (PoliceTimer <= 0.f)
		{
			PoliceTimer = PoliceInterval * Rng.FRandRange(0.7f, 1.5f) * (1.f + (5 - Context.WantedLevel) * 0.25f);
			TArray<const FAegisEntry*> C;
			for (const FAegisEntry& E : PoliceLines)
			{
				if (E.When.Evaluate(Context)) C.Add(&E);
			}
			if (C.Num() > 0)
			{
				FireLine(*C[Rng.RandRange(0, C.Num() - 1)]);
			}
		}
	}
}

// ---------------------------------------------------------------------------------------------
void UMTAAmbienceSubsystem::SetPlaybackEnabled(bool bEnabled)
{
	bPlayback = bEnabled;
	if (!bEnabled)
	{
		for (auto& P : LayerComps)
		{
			if (P.Value) P.Value->Stop();
		}
	}
}

void UMTAAmbienceSubsystem::UpdatePlayback()
{
	UWorld* World = GetWorld();
	if (!bPlayback || !World)
	{
		return;
	}
	for (const auto& P : Layers)
	{
		if (!P.Value.bLoop)
		{
			continue;
		}
		const float G = GetLayerGain(P.Key) * P.Value.BaseGain;
		TObjectPtr<UAudioComponent>* Found = LayerComps.Find(P.Key);
		UAudioComponent* Comp = Found ? Found->Get() : nullptr;
		if (G < 0.003f)
		{
			if (Comp && Comp->IsPlaying()) Comp->Stop();
			continue;
		}
		if (!Comp)
		{
			const FSoftObjectPath Path(AudioPath + P.Value.File + TEXT(".") + P.Value.File);
			USoundBase* S = Cast<USoundBase>(Path.TryLoad());
			if (!S)
			{
				LayerComps.Add(P.Key, nullptr); // nicht erneut versuchen
				continue;
			}
			if (USoundWave* W = Cast<USoundWave>(S)) W->bLooping = true;
			Comp = UGameplayStatics::SpawnSound2D(World, S, G, 1.f, Rng.FRandRange(0.f, 10.f), nullptr, false, false);
			LayerComps.Add(P.Key, Comp);
			continue;
		}
		Comp->SetVolumeMultiplier(G);
		if (!Comp->IsPlaying()) Comp->Play(Rng.FRandRange(0.f, 10.f));
	}
}

void UMTAAmbienceSubsystem::PlayThunder(float Intensity, float DelaySeconds)
{
	UWorld* World = GetWorld();
	const FLayerDef* D = Layers.Find(TEXT("thunder"));
	if (!World || !D)
	{
		return;
	}
	FTimerHandle H;
	const FString File = D->File;
	const FString Base = AudioPath;
	const float Gain = D->BaseGain * FMath::Clamp(Intensity, 0.2f, 1.f) * (1.f - Weather.SoundDamping * 0.5f);
	World->GetTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, File, Base, Gain]()
	{
		const FSoftObjectPath Path(Base + File + TEXT(".") + File);
		if (USoundBase* S = Cast<USoundBase>(Path.TryLoad()))
		{
			UGameplayStatics::PlaySound2D(this, S, Gain);
		}
	}), FMath::Max(0.05f, DelaySeconds), false);
}

void UMTAAmbienceSubsystem::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bLoaded)
	{
		return;
	}
	BindTimer += Dt;
	if (BindTimer > 2.f)
	{
		BindTimer = 0.f;
		BindWeather();
	}
	TMap<FName, float> Target;
	ComputeTargetGains(Target);
	const float K = 1.f - FMath::Exp(-Dt / FMath::Max(0.05f, Smoothing));
	for (const auto& P : Target)
	{
		float& G = Gains.FindOrAdd(P.Key);
		G += (P.Value - G) * K;
	}
	const FMTAReverbParams T = ComputeReverbTarget();
	Reverb.DecaySeconds = FMath::Lerp(Reverb.DecaySeconds, T.DecaySeconds, K);
	Reverb.Wet = FMath::Lerp(Reverb.Wet, T.Wet, K);
	Reverb.PreDelayMs = FMath::Lerp(Reverb.PreDelayMs, T.PreDelayMs, K);
	Reverb.ReflectionGain = FMath::Lerp(Reverb.ReflectionGain, T.ReflectionGain, K);
	Reverb.HighCutHz = FMath::Lerp(Reverb.HighCutHz, T.HighCutHz, K);
	TickAegis(Dt);
	UpdatePlayback();
}
