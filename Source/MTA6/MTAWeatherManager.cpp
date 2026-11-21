#include "MTAWeatherManager.h"
#include "MTAAudioTypes.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"
#include "Particles/ParticleSystemComponent.h" // UFXSystemAsset / UFXSystemComponent

// Niagara ist optional: nur wenn das Modul "Niagara" in MTA6.Build.cs steht, ist der Header sichtbar.
#if defined(__has_include)
#if __has_include("NiagaraFunctionLibrary.h") && __has_include("NiagaraSystem.h")
#include "NiagaraFunctionLibrary.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#define MTA_HAS_NIAGARA 1
#endif
#endif
#ifndef MTA_HAS_NIAGARA
#define MTA_HAS_NIAGARA 0
#endif

TWeakObjectPtr<AMTAWeatherManager> AMTAWeatherManager::CachedManager;

// ---------------------------------------------------------------------------------------------
namespace MTAWeather
{
	static const TCHAR* GStateNames[] = { TEXT("Clear"), TEXT("Cloudy"), TEXT("Haze"), TEXT("Rain"), TEXT("Thunderstorm"), TEXT("SmogFog"), TEXT("DustStorm") };

	FName StateName(EMTAWeatherState State)
	{
		return GStateNames[FMath::Clamp(static_cast<int32>(State), 0, Num() - 1)];
	}

	bool StateFromName(FName Name, EMTAWeatherState& OutState)
	{
		for (int32 I = 0; I < Num(); ++I)
		{
			if (Name == FName(GStateNames[I]))
			{
				OutState = static_cast<EMTAWeatherState>(I);
				return true;
			}
		}
		return false;
	}

	FString StateDisplayName(EMTAWeatherState State)
	{
		static const TCHAR* Names[] = { TEXT("Klar"), TEXT("Bew\u00f6lkt"), TEXT("Dunst"), TEXT("Regen"), TEXT("Gewitter"), TEXT("Smog"), TEXT("Staubsturm") };
		return Names[FMath::Clamp(static_cast<int32>(State), 0, Num() - 1)];
	}
}

// ---------------------------------------------------------------------------------------------
AMTAWeatherManager::AMTAWeatherManager()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
	FMemory::Memzero(MarkovMatrix);
}

FMTAWeatherManagerReady& AMTAWeatherManager::OnManagerReady()
{
	static FMTAWeatherManagerReady Delegate;
	return Delegate;
}

AMTAWeatherManager* AMTAWeatherManager::Find(const UObject* WorldContext)
{
	if (CachedManager.IsValid() && CachedManager->GetWorld() == (WorldContext ? WorldContext->GetWorld() : nullptr))
	{
		return CachedManager.Get();
	}
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World)
	{
		return nullptr;
	}
	for (TActorIterator<AMTAWeatherManager> It(World); It; ++It)
	{
		CachedManager = *It;
		return *It;
	}
	return nullptr;
}

AMTAWeatherManager* AMTAWeatherManager::GetOrSpawn(UObject* WorldContext)
{
	if (AMTAWeatherManager* Existing = Find(WorldContext))
	{
		return Existing;
	}
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	return World ? World->SpawnActor<AMTAWeatherManager>() : nullptr;
}

float AMTAWeatherManager::GetGripMultiplierFor(const UObject* WorldContext)
{
	const AMTAWeatherManager* M = Find(WorldContext);
	return M ? M->Current.GripMultiplier : 1.f;
}

// ---------------------------------------------------------------------------------------------
bool AMTAWeatherManager::LoadWeatherData()
{
	const TSharedPtr<FJsonObject> Root = MTAAudio::LoadJsonFile(DataFile);
	if (!Root.IsValid())
	{
		return false;
	}
	Defs.Reset();
	const TSharedPtr<FJsonObject>* States = nullptr;
	if (!Root->TryGetObjectField(TEXT("states"), States))
	{
		return false;
	}
	for (int32 I = 0; I < MTAWeather::Num(); ++I)
	{
		const EMTAWeatherState S = static_cast<EMTAWeatherState>(I);
		const TSharedPtr<FJsonObject>* O = nullptr;
		if (!(*States)->TryGetObjectField(MTAWeather::StateName(S).ToString(), O))
		{
			continue;
		}
		FMTAWeatherStateDef D;
		FMTAWeatherParams& T = D.Target;
		T.State = S;
		T.TargetState = S;
		T.RainIntensity = MTAAudio::ReadNumber(*O, TEXT("rain"), 0);
		T.FogDensity = MTAAudio::ReadNumber(*O, TEXT("fog"), 0);
		T.WindStrength = MTAAudio::ReadNumber(*O, TEXT("wind"), 0.1);
		T.VisibilityMeters = MTAAudio::ReadNumber(*O, TEXT("visibilityM"), 8000);
		D.GripFactor = MTAAudio::ReadNumber(*O, TEXT("gripFactor"), 1);
		T.SoundDamping = MTAAudio::ReadNumber(*O, TEXT("soundDamping"), 0);
		T.TrafficDensity = MTAAudio::ReadNumber(*O, TEXT("traffic"), 1);
		T.CloudCover = MTAAudio::ReadNumber(*O, TEXT("cloud"), 0);
		T.LightningActivity = MTAAudio::ReadNumber(*O, TEXT("lightning"), 0);
		T.DustAmount = MTAAudio::ReadNumber(*O, TEXT("dust"), 0);
		T.SkyDarkening = MTAAudio::ReadNumber(*O, TEXT("skyDarkening"), 0);
		D.DwellMin = MTAAudio::ReadNumber(*O, TEXT("dwellMin"), 200);
		D.DwellMax = FMath::Max(D.DwellMin, static_cast<float>(MTAAudio::ReadNumber(*O, TEXT("dwellMax"), 400)));
		D.TransitionSeconds = MTAAudio::ReadNumber(*O, TEXT("transitionSeconds"), 40);
		D.Precip = FName(*MTAAudio::ReadString(*O, TEXT("precip"), TEXT("None")));
		Defs.Add(S, D);
	}
	if (Defs.Num() == 0)
	{
		return false;
	}

	// Markov-Matrix
	FMemory::Memzero(MarkovMatrix);
	const TSharedPtr<FJsonObject>* Markov = nullptr;
	if (Root->TryGetObjectField(TEXT("markov"), Markov))
	{
		for (int32 I = 0; I < MTAWeather::Num(); ++I)
		{
			const TSharedPtr<FJsonObject>* Row = nullptr;
			if ((*Markov)->TryGetObjectField(MTAWeather::StateName(static_cast<EMTAWeatherState>(I)).ToString(), Row))
			{
				for (int32 J = 0; J < MTAWeather::Num(); ++J)
				{
					MarkovMatrix[I][J] = MTAAudio::ReadNumber(*Row, *MTAWeather::StateName(static_cast<EMTAWeatherState>(J)).ToString(), 0);
				}
			}
		}
	}

	auto ReadWeights = [](const TSharedPtr<FJsonObject>& O, float* Out)
	{
		for (int32 J = 0; J < 7; ++J)
		{
			Out[J] = static_cast<float>(MTAAudio::ReadNumber(O, *MTAWeather::StateName(static_cast<EMTAWeatherState>(J)).ToString(), 1.0));
		}
	};

	HourBias.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Hours = nullptr;
	if (Root->TryGetArrayField(TEXT("hourBias"), Hours))
	{
		for (const TSharedPtr<FJsonValue>& V : *Hours)
		{
			const TSharedPtr<FJsonObject> O = V->AsObject();
			const TSharedPtr<FJsonObject>* W = nullptr;
			if (!O.IsValid() || !O->TryGetObjectField(TEXT("weights"), W))
			{
				continue;
			}
			FBias B;
			B.From = MTAAudio::ReadNumber(O, TEXT("hourFrom"), 0);
			B.To = MTAAudio::ReadNumber(O, TEXT("hourTo"), 24);
			ReadWeights(*W, B.Weights);
			HourBias.Add(B);
		}
	}
	DistrictBias.Reset();
	const TSharedPtr<FJsonObject>* Districts = nullptr;
	if (Root->TryGetObjectField(TEXT("districtBias"), Districts))
	{
		for (const auto& P : (*Districts)->Values)
		{
			const TSharedPtr<FJsonObject> O = P.Value->AsObject();
			if (O.IsValid())
			{
				TArray<float> W;
				W.SetNum(7);
				ReadWeights(O, W.GetData());
				DistrictBias.Add(FName(*P.Key), W);
			}
		}
	}

	const TSharedPtr<FJsonObject>* Wet = nullptr;
	if (Root->TryGetObjectField(TEXT("wetness"), Wet))
	{
		WetRise = MTAAudio::ReadNumber(*Wet, TEXT("riseRate"), WetRise);
		WetDry = MTAAudio::ReadNumber(*Wet, TEXT("dryRate"), WetDry);
		WetGripLoss = MTAAudio::ReadNumber(*Wet, TEXT("wetGripLoss"), WetGripLoss);
		RoughDry = MTAAudio::ReadNumber(*Wet, TEXT("roughnessDry"), RoughDry);
		RoughWet = MTAAudio::ReadNumber(*Wet, TEXT("roughnessWet"), RoughWet);
	}
	const TSharedPtr<FJsonObject>* Lt = nullptr;
	if (Root->TryGetObjectField(TEXT("lightning"), Lt))
	{
		LightningMin = MTAAudio::ReadNumber(*Lt, TEXT("minIntervalSeconds"), LightningMin);
		LightningMax = MTAAudio::ReadNumber(*Lt, TEXT("maxIntervalSeconds"), LightningMax);
		ThunderPerKm = MTAAudio::ReadNumber(*Lt, TEXT("thunderDelayPerKm"), ThunderPerKm);
		LightningMaxKm = MTAAudio::ReadNumber(*Lt, TEXT("maxDistanceKm"), LightningMaxKm);
	}
	const float WindDir = MTAAudio::ReadNumber(Root, TEXT("windDirectionDegrees"), 255);

	Rng.Initialize(SeedOverride != 0 ? SeedOverride : static_cast<int32>(MTAAudio::ReadNumber(Root, TEXT("seed"), 4711)));

	EMTAWeatherState Start = EMTAWeatherState::Clear;
	MTAWeather::StateFromName(FName(*MTAAudio::ReadString(Root, TEXT("initialState"), TEXT("Clear"))), Start);
	const FMTAWeatherStateDef* D = Defs.Find(Start);
	Current = D ? D->Target : FMTAWeatherParams();
	Current.WindDirectionDegrees = WindDir;
	GripFactorCurrent = GripFactorFrom = D ? D->GripFactor : 1.f;
	BlendFrom = Current;
	PrevState = Start;
	TransitionElapsed = TransitionDuration = 0.f;
	Wetness = Current.RainIntensity > 0.05f ? 0.6f : 0.f;
	ScheduleDwell();
	bLoaded = true;
	UpdateWetnessAndDerived(0.f);
	return true;
}

void AMTAWeatherManager::BeginPlay()
{
	Super::BeginPlay();
	if (!LoadWeatherData())
	{
		UE_LOG(LogTemp, Warning, TEXT("AMTAWeatherManager: weather.json konnte nicht geladen werden, Standardwetter bleibt aktiv."));
	}
	CachedManager = this;
	ApplyVisuals();
	OnManagerReady().Broadcast(this);
}

void AMTAWeatherManager::EndPlay(const EEndPlayReason::Type Reason)
{
	if (CachedManager.Get() == this)
	{
		CachedManager.Reset();
	}
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------------
void AMTAWeatherManager::ScheduleDwell()
{
	const FMTAWeatherStateDef* D = Defs.Find(Current.TargetState);
	DwellRemaining = D ? Rng.FRandRange(D->DwellMin, D->DwellMax) * DwellScale : 300.f;
}

EMTAWeatherState AMTAWeatherManager::PickNextState()
{
	const int32 Cur = static_cast<int32>(Current.TargetState);
	float W[7];
	float Sum = 0.f;
	for (int32 J = 0; J < 7; ++J)
	{
		W[J] = (J == Cur || !Defs.Contains(static_cast<EMTAWeatherState>(J))) ? 0.f : MarkovMatrix[Cur][J];
		for (const FBias& B : HourBias)
		{
			if (MTAAudio::HourInRange(GameHour, B.From, B.To))
			{
				W[J] *= B.Weights[J];
			}
		}
		if (const TArray<float>* DB = DistrictBias.Find(CurrentDistrict))
		{
			W[J] *= (*DB)[J];
		}
		Sum += W[J];
	}
	if (Sum <= KINDA_SMALL_NUMBER)
	{
		return Current.TargetState;
	}
	float R = Rng.FRand() * Sum;
	for (int32 J = 0; J < 7; ++J)
	{
		R -= W[J];
		if (R <= 0.f && W[J] > 0.f)
		{
			return static_cast<EMTAWeatherState>(J);
		}
	}
	return static_cast<EMTAWeatherState>(Cur);
}

void AMTAWeatherManager::SetWeather(EMTAWeatherState NewState, float TransitionSeconds)
{
	const FMTAWeatherStateDef* D = Defs.Find(NewState);
	if (!D)
	{
		return;
	}
	BeginTransition(NewState, TransitionSeconds >= 0.f ? TransitionSeconds : D->TransitionSeconds);
}

void AMTAWeatherManager::BeginTransition(EMTAWeatherState NewState, float Seconds)
{
	const FMTAWeatherStateDef* D = Defs.Find(NewState);
	if (!D)
	{
		return;
	}
	const EMTAWeatherState Old = Current.TargetState;
	BlendFrom = Current; // aktueller (evtl. halb geblendeter) Zustand als Startpunkt
	GripFactorFrom = GripFactorCurrent;
	PrevState = Current.State;
	Current.State = Old != NewState ? Current.State : NewState;
	Current.TargetState = NewState;
	TransitionDuration = FMath::Max(0.01f, Seconds);
	TransitionElapsed = 0.f;
	Current.TransitionAlpha = 0.f;
	ScheduleDwell();
	if (Old != NewState)
	{
		OnWeatherStateChanged.Broadcast(Old, NewState);
	}
}

void AMTAWeatherManager::ApplyBlend(float Alpha)
{
	const FMTAWeatherStateDef* D = Defs.Find(Current.TargetState);
	if (!D)
	{
		return;
	}
	const FMTAWeatherParams& T = D->Target;
	const FMTAWeatherParams& F = BlendFrom;
	Current.TransitionAlpha = Alpha;
	Current.RainIntensity = FMath::Lerp(F.RainIntensity, T.RainIntensity, Alpha);
	Current.FogDensity = FMath::Lerp(F.FogDensity, T.FogDensity, Alpha);
	Current.WindStrength = FMath::Lerp(F.WindStrength, T.WindStrength, Alpha);
	Current.VisibilityMeters = FMath::Lerp(F.VisibilityMeters, T.VisibilityMeters, Alpha);
	Current.SoundDamping = FMath::Lerp(F.SoundDamping, T.SoundDamping, Alpha);
	Current.TrafficDensity = FMath::Lerp(F.TrafficDensity, T.TrafficDensity, Alpha);
	Current.CloudCover = FMath::Lerp(F.CloudCover, T.CloudCover, Alpha);
	Current.LightningActivity = FMath::Lerp(F.LightningActivity, T.LightningActivity, Alpha);
	Current.DustAmount = FMath::Lerp(F.DustAmount, T.DustAmount, Alpha);
	Current.SkyDarkening = FMath::Lerp(F.SkyDarkening, T.SkyDarkening, Alpha);
	GripFactorCurrent = FMath::Lerp(GripFactorFrom, D->GripFactor, Alpha);
	if (Alpha >= 1.f)
	{
		Current.State = Current.TargetState;
	}
}

void AMTAWeatherManager::UpdateWetnessAndDerived(float Dt)
{
	if (Current.RainIntensity > 0.05f)
	{
		Wetness += WetRise * Current.RainIntensity * Dt;
	}
	else
	{
		Wetness -= WetDry * (1.f + Current.WindStrength) * Dt;
	}
	Wetness = FMath::Clamp(Wetness, 0.f, 1.f);
	Current.Wetness = Wetness;
	Current.RoadRoughness = FMath::Lerp(RoughDry, RoughWet, Wetness);
	Current.GripMultiplier = FMath::Lerp(1.f, 1.f - WetGripLoss, Wetness) * GripFactorCurrent;
	const float Noise = FMath::PerlinNoise1D(Clock * 0.35f) * 0.5f + 0.5f; // 0..1
	Current.WindGust = FMath::Clamp(Current.WindStrength * (0.65f + 0.7f * Noise), 0.f, 1.f);
}

void AMTAWeatherManager::UpdateLightning(float Dt)
{
	if (Current.LightningActivity < 0.05f)
	{
		return;
	}
	LightningTimer -= Dt;
	if (LightningTimer <= 0.f)
	{
		const float Km = Rng.FRandRange(0.3f, LightningMaxKm);
		OnLightning.Broadcast(Rng.FRandRange(0.5f, 1.f), Km * ThunderPerKm);
		LightningTimer = Rng.FRandRange(LightningMin, LightningMax) / FMath::Max(0.25f, Current.LightningActivity);
	}
}

void AMTAWeatherManager::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!bLoaded)
	{
		return;
	}
	Clock += Dt;
	if (bUseOwnClock)
	{
		GameHour = FMath::Fmod(GameHour + Dt * 24.f / OwnDayLengthSeconds, 24.f);
	}

	if (TransitionElapsed < TransitionDuration)
	{
		TransitionElapsed += Dt;
		const float A = FMath::Clamp(TransitionElapsed / TransitionDuration, 0.f, 1.f);
		ApplyBlend(FMath::InterpEaseInOut(0.f, 1.f, A, 2.f));
		if (A >= 1.f)
		{
			ApplyBlend(1.f);
		}
	}
	else if (bAutoCycle)
	{
		DwellRemaining -= Dt;
		if (DwellRemaining <= 0.f)
		{
			const EMTAWeatherState Next = PickNextState();
			const FMTAWeatherStateDef* D = Defs.Find(Next);
			BeginTransition(Next, D ? D->TransitionSeconds : 40.f);
		}
	}

	UpdateWetnessAndDerived(Dt);
	UpdateLightning(Dt);
	ApplyVisuals();

	NotifyAccumulator += Dt;
	if (NotifyAccumulator >= 0.1f)
	{
		NotifyAccumulator = 0.f;
		NotifyConsumers();
	}
}

// ---------------------------------------------------------------------------------------------
void AMTAWeatherManager::RegisterConsumer(UObject* Consumer)
{
	if (Consumer && Consumer->Implements<UMTAWeatherConsumer>())
	{
		Consumers.AddUnique(Consumer);
		IMTAWeatherConsumer::Execute_OnWeatherUpdated(Consumer, Current);
	}
}

void AMTAWeatherManager::UnregisterConsumer(UObject* Consumer)
{
	Consumers.Remove(Consumer);
}

void AMTAWeatherManager::NotifyConsumers()
{
	for (int32 I = Consumers.Num() - 1; I >= 0; --I)
	{
		UObject* C = Consumers[I].Get();
		if (!C)
		{
			Consumers.RemoveAtSwap(I);
			continue;
		}
		IMTAWeatherConsumer::Execute_OnWeatherUpdated(C, Current);
	}
}

// ---------------------------------------------------------------------------------------------
void AMTAWeatherManager::EnsurePrecipitationComponent()
{
#if MTA_HAS_NIAGARA
	const EMTAWeatherState Kind = Current.DustAmount > 0.3f ? EMTAWeatherState::DustStorm : EMTAWeatherState::Rain;
	const bool bWant = Current.RainIntensity > 0.02f || Current.DustAmount > 0.05f;
	if (!bWant)
	{
		if (PrecipComponent)
		{
			PrecipComponent->Deactivate();
		}
		return;
	}
	if (PrecipComponent && PrecipKindShown == Kind)
	{
		if (!PrecipComponent->IsActive())
		{
			PrecipComponent->Activate();
		}
		return;
	}
	if (PrecipComponent)
	{
		PrecipComponent->DestroyComponent();
		PrecipComponent = nullptr;
	}
	UFXSystemAsset* Asset = (Kind == EMTAWeatherState::DustStorm ? DustSystem : RainSystem).LoadSynchronous();
	if (UNiagaraSystem* Sys = Cast<UNiagaraSystem>(Asset))
	{
		PrecipComponent = UNiagaraFunctionLibrary::SpawnSystemAttached(Sys, GetRootComponent(), NAME_None, FVector::ZeroVector,
			FRotator::ZeroRotator, EAttachLocation::KeepRelativeOffset, false);
		PrecipKindShown = Kind;
	}
#endif
}

void AMTAWeatherManager::ApplyVisuals()
{
	// Emitter folgt der Kamera
	if (APlayerCameraManager* Cam = UGameplayStatics::GetPlayerCameraManager(this, 0))
	{
		SetActorLocation(Cam->GetCameraLocation() + FVector(0.f, 0.f, PrecipHeightAboveCamera));
	}
	EnsurePrecipitationComponent();
	if (PrecipComponent)
	{
		PrecipComponent->SetFloatParameter(TEXT("Intensity"), FMath::Max(Current.RainIntensity, Current.DustAmount));
		PrecipComponent->SetFloatParameter(TEXT("WindStrength"), Current.WindGust);
	}

	if (WeatherParameters)
	{
		UKismetMaterialLibrary::SetScalarParameterValue(this, WeatherParameters, TEXT("Wetness"), Current.Wetness);
		UKismetMaterialLibrary::SetScalarParameterValue(this, WeatherParameters, TEXT("RoadRoughness"), Current.RoadRoughness);
		UKismetMaterialLibrary::SetScalarParameterValue(this, WeatherParameters, TEXT("RainIntensity"), Current.RainIntensity);
		UKismetMaterialLibrary::SetScalarParameterValue(this, WeatherParameters, TEXT("FogDensity"), Current.FogDensity);
		UKismetMaterialLibrary::SetScalarParameterValue(this, WeatherParameters, TEXT("WindStrength"), Current.WindGust);
		UKismetMaterialLibrary::SetScalarParameterValue(this, WeatherParameters, TEXT("DustAmount"), Current.DustAmount);
	}
	if (FogActor && FogActor->GetComponent())
	{
		FogActor->GetComponent()->SetFogDensity(Current.FogDensity * FogDensityScale);
	}
	OnPrecipitationChanged(Current.RainIntensity, Current.DustAmount);
}
