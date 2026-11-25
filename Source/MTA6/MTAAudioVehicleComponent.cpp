#include "MTAAudioVehicleComponent.h"
#include "MTAWeatherManager.h"
#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "UObject/SoftObjectPath.h"

namespace MTAVehicleAudio
{
	// Standard-Assets (tools/audio/make_vehicle_sounds.py + tools/ue_audio/import_vehicle_sounds.py)
	static const TCHAR* EngineLayerPaths[] =
	{
		TEXT("/Game/MTA6/Audio/Vehicle/veh_engine_0800.veh_engine_0800"),
		TEXT("/Game/MTA6/Audio/Vehicle/veh_engine_2000.veh_engine_2000"),
		TEXT("/Game/MTA6/Audio/Vehicle/veh_engine_3500.veh_engine_3500"),
		TEXT("/Game/MTA6/Audio/Vehicle/veh_engine_5500.veh_engine_5500"),
	};
	static const float EngineLayerRPMValues[] = { 800.f, 2000.f, 3500.f, 5500.f };
	static_assert(UE_ARRAY_COUNT(EngineLayerPaths) == UE_ARRAY_COUNT(EngineLayerRPMValues), "Motor-Loops und Drehzahlen muessen zusammenpassen");
	static const TCHAR* TirePath = TEXT("/Game/MTA6/Audio/Vehicle/veh_tire_squeal.veh_tire_squeal");
	static const TCHAR* WindPath = TEXT("/Game/MTA6/Audio/Vehicle/veh_wind.veh_wind");
	static const TCHAR* HornPath = TEXT("/Game/MTA6/Audio/Vehicle/veh_horn.veh_horn");
	static const TCHAR* SirenPath = TEXT("/Game/MTA6/Audio/Vehicle/veh_siren.veh_siren");
	static const TCHAR* CollisionPath = TEXT("/Game/MTA6/Audio/Vehicle/veh_collision.veh_collision");
	static const TCHAR* DoorPath = TEXT("/Game/MTA6/Audio/Vehicle/veh_door.veh_door");
	static const TCHAR* AttenuationPath = TEXT("/Game/MTA6/Audio/Vehicle/ATT_Vehicle.ATT_Vehicle");

	/** Gewicht, ab dem eine Schicht als "laut" zaehlt (Selbsttest). */
	static constexpr float AudibleGain = 0.05f;

	/** Prueft vor dem Laden, ob das Paket existiert (vermeidet Lade-Fehlermeldungen). */
	static UObject* TryLoad(const FSoftObjectPath& Path)
	{
		if (Path.IsNull())
		{
			return nullptr;
		}
		if (UObject* Already = Path.ResolveObject())
		{
			return Already;
		}
		if (!FPackageName::DoesPackageExist(Path.GetLongPackageName()))
		{
			return nullptr;
		}
		return Path.TryLoad();
	}
}

UMTAVehicleAudioComponent::UMTAVehicleAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	// Standard-Assets als weiche Referenzen: ohne Blueprint nutzbar, fehlende Assets -> Stille + Warnung
	const int32 NumDefaultLayers = static_cast<int32>(UE_ARRAY_COUNT(MTAVehicleAudio::EngineLayerPaths));
	for (int32 I = 0; I < NumDefaultLayers; ++I)
	{
		EngineLayerSounds.Add(TSoftObjectPtr<USoundBase>(FSoftObjectPath(MTAVehicleAudio::EngineLayerPaths[I])));
		EngineLayerRPMs.Add(MTAVehicleAudio::EngineLayerRPMValues[I]);
	}
	TireSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MTAVehicleAudio::TirePath));
	WindSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MTAVehicleAudio::WindPath));
	HornSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MTAVehicleAudio::HornPath));
	SirenSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MTAVehicleAudio::SirenPath));
	CollisionSound = TSoftObjectPtr<USoundBase>(FSoftObjectPath(MTAVehicleAudio::CollisionPath));
	Attenuation = TSoftObjectPtr<USoundAttenuation>(FSoftObjectPath(MTAVehicleAudio::AttenuationPath));
}

// ---------------------------------------------------------------------------------------
// Mischung
// ---------------------------------------------------------------------------------------

void UMTAVehicleAudioComponent::ComputeEngineLayerGains(float RPMNorm, float& Low, float& Mid, float& High)
{
	const float X = FMath::Clamp(RPMNorm, 0.f, 1.f);
	const float HalfPi = UE_PI * 0.5f;
	if (X < 0.5f)
	{
		const float T = X / 0.5f;
		Low = FMath::Cos(T * HalfPi);
		Mid = FMath::Sin(T * HalfPi);
		High = 0.f;
	}
	else
	{
		const float T = (X - 0.5f) / 0.5f;
		Low = 0.f;
		Mid = FMath::Cos(T * HalfPi);
		High = FMath::Sin(T * HalfPi);
	}
}

void UMTAVehicleAudioComponent::ComputeEngineLayerMix(float InRPM, const TArray<float>& LayerRPMs, TArray<float>& OutGains, TArray<float>& OutPitches)
{
	const int32 Num = LayerRPMs.Num();
	OutGains.Init(0.f, Num);
	OutPitches.Init(1.f, Num);
	if (Num == 0)
	{
		return;
	}
	const float R = FMath::Max(0.f, InRPM);
	for (int32 I = 0; I < Num; ++I)
	{
		OutPitches[I] = FMath::Clamp(R / FMath::Max(1.f, LayerRPMs[I]), 0.5f, 2.f);
	}
	if (Num == 1 || R <= LayerRPMs[0])
	{
		OutGains[0] = 1.f;
		return;
	}
	if (R >= LayerRPMs[Num - 1])
	{
		OutGains[Num - 1] = 1.f;
		return;
	}
	for (int32 I = 0; I < Num - 1; ++I)
	{
		const float A = LayerRPMs[I];
		const float B = LayerRPMs[I + 1];
		if (R >= A && R <= B)
		{
			const float T = FMath::Clamp((R - A) / FMath::Max(1.f, B - A), 0.f, 1.f);
			OutGains[I] = FMath::Cos(T * UE_PI * 0.5f);
			OutGains[I + 1] = FMath::Sin(T * UE_PI * 0.5f);
			return;
		}
	}
	OutGains[0] = 1.f; // nicht aufsteigend sortiert: sicherer Rueckfall
}

TArray<float> UMTAVehicleAudioComponent::GetDefaultEngineLayerRPMs()
{
	TArray<float> Result;
	for (const float V : MTAVehicleAudio::EngineLayerRPMValues)
	{
		Result.Add(V);
	}
	return Result;
}

void UMTAVehicleAudioComponent::OnWeatherUpdated_Implementation(const FMTAWeatherParams& Params)
{
	Weather = Params;
}

// ---------------------------------------------------------------------------------------
// Komponenten anlegen / steuern
// ---------------------------------------------------------------------------------------

USoundBase* UMTAVehicleAudioComponent::LoadSoundSafe(const TSoftObjectPtr<USoundBase>& Asset, const TCHAR* What)
{
	if (Asset.IsNull())
	{
		return nullptr;
	}
	USoundBase* Sound = Cast<USoundBase>(MTAVehicleAudio::TryLoad(Asset.ToSoftObjectPath()));
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA Fahrzeugsound: %s fehlt (%s) - bleibt stumm. tools/ue_audio/import_vehicle_sounds.py ausfuehren."),
			What, *Asset.ToString());
	}
	return Sound;
}

UAudioComponent* UMTAVehicleAudioComponent::MakeComp(USoundBase* Sound, const TCHAR* CompName)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !Sound)
	{
		return nullptr;
	}
	UAudioComponent* C = NewObject<UAudioComponent>(OwnerActor, MakeUniqueObjectName(OwnerActor, UAudioComponent::StaticClass(), FName(CompName)));
	C->bAutoActivate = false;
	C->bAutoDestroy = false;
	C->bStopWhenOwnerDestroyed = true;
	C->SetSound(Sound);
	C->SetVolumeMultiplier(0.f);
	if (LoadedAttenuation)
	{
		C->AttenuationSettings = LoadedAttenuation;
	}
	if (USceneComponent* RootComp = OwnerActor->GetRootComponent())
	{
		C->SetupAttachment(RootComp);
	}
	C->RegisterComponent();
	return C;
}

UAudioComponent* UMTAVehicleAudioComponent::GetEngineSlot(int32 Slot) const
{
	return Slot == 0 ? EngineSlotA.Get() : EngineSlotB.Get();
}

void UMTAVehicleAudioComponent::CreateAudioComponents()
{
	if (bComponentsCreated)
	{
		return;
	}
	bComponentsCreated = true;
	if (!bDriveAudioComponents)
	{
		return;
	}

	LoadedAttenuation = Cast<USoundAttenuation>(MTAVehicleAudio::TryLoad(Attenuation.ToSoftObjectPath()));
	if (!LoadedAttenuation && !Attenuation.IsNull())
	{
		UE_LOG(LogTemp, Warning, TEXT("MTA Fahrzeugsound: Daempfung fehlt (%s), Klang ohne Entfernungsdaempfung."), *Attenuation.ToString());
	}

	// Motor-Schichten: nur gueltige Paare (Sound + Drehzahl) uebernehmen, aufsteigend
	LoadedEngineLayers.Reset();
	LoadedLayerRPMs.Reset();
	const int32 NumLayers = FMath::Min(EngineLayerSounds.Num(), EngineLayerRPMs.Num());
	for (int32 I = 0; I < NumLayers; ++I)
	{
		if (USoundBase* S = LoadSoundSafe(EngineLayerSounds[I], TEXT("Motor-Loop")))
		{
			if (LoadedLayerRPMs.Num() == 0 || EngineLayerRPMs[I] > LoadedLayerRPMs.Last())
			{
				LoadedEngineLayers.Add(S);
				LoadedLayerRPMs.Add(EngineLayerRPMs[I]);
			}
		}
	}

	bSingleEngineSource = false;
	if (LoadedEngineLayers.Num() > 0)
	{
		EngineSlotA = MakeComp(LoadedEngineLayers[0], TEXT("MTAEngineSlotA"));
		SlotLayer[0] = EngineSlotA ? 0 : -1;
		if (LoadedEngineLayers.Num() > 1)
		{
			EngineSlotB = MakeComp(LoadedEngineLayers[1], TEXT("MTAEngineSlotB"));
			SlotLayer[1] = EngineSlotB ? 1 : -1;
		}
	}
	else if (USoundBase* Single = LoadSoundSafe(EngineSound, TEXT("Motorquelle")))
	{
		EngineSlotA = MakeComp(Single, TEXT("MTAEngineSlotA"));
		bSingleEngineSource = EngineSlotA != nullptr;
	}

	TireComp = MakeComp(LoadSoundSafe(TireSound, TEXT("Reifenquietschen")), TEXT("MTATireAudio"));
	WindComp = MakeComp(LoadSoundSafe(WindSound, TEXT("Fahrtwind")), TEXT("MTAWindAudio"));
	HornComp = MakeComp(LoadSoundSafe(HornSound, TEXT("Hupe")), TEXT("MTAHornAudio"));
	LoadedCollision = LoadSoundSafe(CollisionSound, TEXT("Aufprall"));
	// Sirene wird erst bei Bedarf angelegt (SetSiren), damit normale Autos nur 5 Komponenten haben.
}

int32 UMTAVehicleAudioComponent::GetNumAudioComponents() const
{
	int32 Count = 0;
	for (const UAudioComponent* C : { EngineSlotA.Get(), EngineSlotB.Get(), TireComp.Get(), WindComp.Get(), HornComp.Get(), SirenComp.Get() })
	{
		Count += C ? 1 : 0;
	}
	return Count;
}

void UMTAVehicleAudioComponent::SetAudioActive(bool bActive)
{
	if (bActive == bAudioActive)
	{
		return;
	}
	bAudioActive = bActive;
	if (bActive)
	{
		CreateAudioComponents();
		// Motorstart: Drehzahl von unten hochlaufen lassen
		SmoothRPM = Config.IdleRPM * 0.4f;
		SmoothLoad = 0.f;
		const float StartFade = 0.35f;
		for (UAudioComponent* C : { EngineSlotA.Get(), EngineSlotB.Get() })
		{
			if (C)
			{
				C->FadeIn(StartFade, 1.f);
			}
		}
		for (UAudioComponent* C : { TireComp.Get(), WindComp.Get() })
		{
			if (C)
			{
				C->Play();
			}
		}
	}
	else
	{
		StopAllAudio(0.4f);
		bHorn = false;
	}
}

void UMTAVehicleAudioComponent::StopAllAudio(float FadeSeconds)
{
	for (UAudioComponent* C : { EngineSlotA.Get(), EngineSlotB.Get(), TireComp.Get(), WindComp.Get(), HornComp.Get(), SirenComp.Get() })
	{
		if (C && C->IsPlaying())
		{
			if (FadeSeconds > 0.f)
			{
				C->FadeOut(FadeSeconds, 0.f);
			}
			else
			{
				C->Stop();
			}
		}
	}
	bHornPlaying = false;
	bSirenPlaying = false;
}

void UMTAVehicleAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	if (AMTAWeatherManager* M = AMTAWeatherManager::Find(this))
	{
		BoundManager = M;
		M->RegisterConsumer(this);
	}
	else
	{
		ManagerReadyHandle = AMTAWeatherManager::OnManagerReady().AddWeakLambda(this, [this](AMTAWeatherManager* Mgr)
		{
			if (Mgr && Mgr->GetWorld() == GetWorld())
			{
				BoundManager = Mgr;
				Mgr->RegisterConsumer(this);
			}
		});
	}
}

void UMTAVehicleAudioComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	AMTAWeatherManager::OnManagerReady().Remove(ManagerReadyHandle);
	if (AMTAWeatherManager* M = BoundManager.Get())
	{
		M->UnregisterConsumer(this);
	}
	for (UAudioComponent* C : { EngineSlotA.Get(), EngineSlotB.Get(), TireComp.Get(), WindComp.Get(), HornComp.Get(), SirenComp.Get() })
	{
		if (C)
		{
			C->Stop();
			C->DestroyComponent();
		}
	}
	EngineSlotA = nullptr;
	EngineSlotB = nullptr;
	TireComp = nullptr;
	WindComp = nullptr;
	HornComp = nullptr;
	SirenComp = nullptr;
	bComponentsCreated = false;
	bAudioActive = false;
	Super::EndPlay(Reason);
}

// ---------------------------------------------------------------------------------------
// Aufprall
// ---------------------------------------------------------------------------------------

void UMTAVehicleAudioComponent::NotifyCollision(float Impulse)
{
	const float Strength = (Impulse - Config.CollisionMinImpulse) / FMath::Max(1.f, Config.CollisionMaxImpulse - Config.CollisionMinImpulse);
	if (Impulse >= Config.CollisionMinImpulse)
	{
		PlayCollision(FMath::Clamp(Strength, 0.f, 1.f));
	}
}

void UMTAVehicleAudioComponent::NotifyImpactSpeed(float ImpactSpeedCms)
{
	if (ImpactSpeedCms < Config.CollisionMinSpeed)
	{
		return;
	}
	const float Strength = (ImpactSpeedCms - Config.CollisionMinSpeed) / FMath::Max(1.f, Config.CollisionMaxSpeed - Config.CollisionMinSpeed);
	PlayCollision(FMath::Clamp(Strength, 0.f, 1.f));
}

void UMTAVehicleAudioComponent::PlayCollision(float Strength)
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (Now - LastCollisionTime < Config.CollisionCooldown)
	{
		return;
	}
	LastCollisionTime = Now;
	Output.CollisionStrength = FMath::Max(Output.CollisionStrength, Strength);
	const float Pitch = FMath::Lerp(1.15f, 0.8f, Strength); // schwere Treffer klingen tiefer
	OnCollisionSound.Broadcast(Strength, Pitch);

	// Nur das aktive (vom Spieler gefahrene) Fahrzeug spielt den Schlag, sonst doppelt bei Auto-gegen-Auto.
	const AActor* OwnerActor = GetOwner();
	if (!bAudioActive || !bDriveAudioComponents || !OwnerActor)
	{
		return;
	}
	if (!LoadedCollision)
	{
		return; // Warnung kam bereits beim Anlegen
	}
	const float Vol = FMath::Lerp(0.3f, 1.f, Strength) * (1.f - Weather.SoundDamping * 0.5f) * MasterVolume;
	UGameplayStatics::PlaySoundAtLocation(this, LoadedCollision, OwnerActor->GetActorLocation(), Vol, Pitch, 0.f, LoadedAttenuation.Get());
}

// ---------------------------------------------------------------------------------------
// Pro Frame
// ---------------------------------------------------------------------------------------

void UMTAVehicleAudioComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(Dt, TickType, ThisTick);

	// Nur hoerbar, wenn der Spieler im Fahrzeug sitzt (Pawn ist dann possessed)
	bool bWantActive = true;
	if (bOnlyWhenPlayerControlled)
	{
		const APawn* OwnerPawn = Cast<APawn>(GetOwner());
		bWantActive = OwnerPawn && OwnerPawn->IsPlayerControlled();
	}
	if (bWantActive != bAudioActive)
	{
		SetAudioActive(bWantActive);
	}
	if (!bAudioActive)
	{
		return; // geparkte Fahrzeuge kosten nur diesen Test
	}

	SmoothRPM = FMath::FInterpTo(SmoothRPM, bEngineRunning ? TargetRPM : 0.f, Dt, Config.RPMResponse);
	SmoothLoad = FMath::FInterpTo(SmoothLoad, TargetLoad, Dt, Config.LoadResponse);

	FMTAVehicleAudioOutput& O = Output;
	O.WeatherDamping = Weather.SoundDamping;
	O.RPM = SmoothRPM;
	const float Span = FMath::Max(1.f, Config.RedlineRPM - Config.IdleRPM);
	O.RPMNorm = bEngineRunning ? FMath::Clamp((SmoothRPM - Config.IdleRPM) / Span, 0.f, 1.f) : 0.f;
	O.Load = SmoothLoad;
	ComputeEngineLayerGains(O.RPMNorm, O.EngineGainLow, O.EngineGainMid, O.EngineGainHigh);
	O.EngineVolume = bEngineRunning ? FMath::Lerp(Config.IdleLoudness, 1.f, SmoothLoad) : 0.f;
	O.EnginePitch = FMath::Lerp(Config.MinPitch, Config.MaxPitch, O.RPMNorm);

	// Reifen: Schlupf ueber Schwelle, nur bei Bewegung; nasse Strasse quietscht frueher.
	const float Thr = FMath::Max(0.05f, Config.SquealSlipThreshold - 0.1f * Weather.Wetness);
	const float SlipAmt = FMath::Clamp((Slip - Thr) / FMath::Max(0.05f, 1.f - Thr), 0.f, 1.f);
	const float MoveAmt = FMath::Clamp(SpeedCms / FMath::Max(1.f, Config.SquealMinSpeed * 3.f), 0.f, 1.f) * (SpeedCms > Config.SquealMinSpeed ? 1.f : 0.f);
	const float SquealTarget = FMath::SmoothStep(0.f, 1.f, SlipAmt) * MoveAmt * (1.f - 0.5f * Weather.Wetness);
	O.TireSquealGain = FMath::FInterpTo(O.TireSquealGain, SquealTarget, Dt, 12.f);
	O.TireSquealPitch = FMath::Lerp(0.85f, 1.25f, Slip);

	// Wind nach Tempo (cm/s -> km/h = *0.036) und Windstaerke
	const float Kmh = SpeedCms * 0.036f;
	const float Wt = FMath::Clamp((Kmh - Config.WindStartKmh) / FMath::Max(1.f, Config.WindFullKmh - Config.WindStartKmh), 0.f, 1.f);
	O.WindGain = FMath::Clamp(FMath::Pow(Wt, 1.5f) + Weather.WindGust * 0.25f * FMath::Min(1.f, Kmh / 30.f), 0.f, 1.f);
	O.RainOnBodyGain = Weather.RainIntensity * FMath::Clamp(0.3f + Kmh / 120.f, 0.f, 1.f);

	O.HornGain = FMath::FInterpTo(O.HornGain, bHorn ? 1.f : 0.f, Dt, 30.f);
	O.SirenGain = FMath::FInterpTo(O.SirenGain, bSiren ? 1.f : 0.f, Dt, 10.f);
	O.CollisionStrength = FMath::FInterpTo(O.CollisionStrength, 0.f, Dt, 3.f);

	PushToAudio();
}

void UMTAVehicleAudioComponent::UpdateEngineSlots(float OutScale)
{
	if (bSingleEngineSource)
	{
		if (UAudioComponent* C = EngineSlotA.Get())
		{
			C->SetFloatParameter(ParamRPM, Output.RPMNorm);
			C->SetFloatParameter(ParamLoad, Output.Load);
			C->SetFloatParameter(ParamGainLow, Output.EngineGainLow);
			C->SetFloatParameter(ParamGainMid, Output.EngineGainMid);
			C->SetFloatParameter(ParamGainHigh, Output.EngineGainHigh);
			C->SetVolumeMultiplier(Output.EngineVolume * OutScale);
			C->SetPitchMultiplier(Output.EnginePitch);
		}
		return;
	}
	const int32 NumLayers = LoadedEngineLayers.Num();
	if (NumLayers == 0)
	{
		return;
	}

	// Beim Starten/Abwuergen unter Leerlauf nicht zu tief pitchen
	const float EffRPM = FMath::Max(SmoothRPM, Config.IdleRPM * 0.4f);
	ComputeEngineLayerMix(EffRPM, LoadedLayerRPMs, MixGains, MixPitches);

	// Aktives Band: untere Schicht B und B+1. Schicht K liegt immer in Slot K % 2; beim Bandwechsel
	// wird nur der Slot neu belegt, dessen alte Schicht gerade Gewicht 0 hat (klickfrei).
	int32 Band = 0;
	for (int32 I = 0; I < NumLayers - 1; ++I)
	{
		if (EffRPM >= LoadedLayerRPMs[I])
		{
			Band = I;
		}
	}
	const float Vol = Output.EngineVolume * OutScale;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		UAudioComponent* C = GetEngineSlot(Slot);
		if (!C)
		{
			continue;
		}
		const int32 Wanted = (Band % 2 == Slot) ? Band : Band + 1;
		if (Wanted >= NumLayers)
		{
			C->SetVolumeMultiplier(0.f);
			continue;
		}
		if (SlotLayer[Slot] != Wanted)
		{
			SlotLayer[Slot] = Wanted;
			const bool bWasPlaying = C->IsPlaying();
			C->SetVolumeMultiplier(0.f);
			C->SetSound(LoadedEngineLayers[Wanted]);
			if (!bWasPlaying)
			{
				C->Play();
			}
		}
		C->SetVolumeMultiplier(MixGains[Wanted] * Vol);
		C->SetPitchMultiplier(MixPitches[Wanted]);
	}
}

void UMTAVehicleAudioComponent::UpdateOneShotLoop(UAudioComponent* Comp, bool bWanted, bool& bPlayingFlag, float FadeIn, float FadeOutSeconds)
{
	if (!Comp || bWanted == bPlayingFlag)
	{
		return;
	}
	bPlayingFlag = bWanted;
	if (bWanted)
	{
		Comp->FadeIn(FadeIn, 1.f);
	}
	else
	{
		Comp->FadeOut(FadeOutSeconds, 0.f);
	}
}

void UMTAVehicleAudioComponent::PushToAudio()
{
	if (!bDriveAudioComponents)
	{
		return;
	}
	const float OutScale = (1.f - Output.WeatherDamping * 0.5f) * MasterVolume;
	UpdateEngineSlots(OutScale);

	if (TireComp)
	{
		TireComp->SetVolumeMultiplier(Output.TireSquealGain * 0.8f * OutScale);
		TireComp->SetPitchMultiplier(Output.TireSquealPitch);
	}
	if (WindComp)
	{
		WindComp->SetVolumeMultiplier(Output.WindGain * 0.6f * MasterVolume);
		WindComp->SetPitchMultiplier(FMath::Lerp(0.85f, 1.2f, Output.WindGain));
	}

	// Hupe und Sirene laufen nur, solange sie gebraucht werden (spart Stimmen)
	if (HornComp)
	{
		HornComp->SetVolumeMultiplier(0.8f * OutScale);
		UpdateOneShotLoop(HornComp, bHorn, bHornPlaying, 0.02f, 0.06f);
	}
	if (bSiren && !SirenComp && GetNumAudioComponents() < 6)
	{
		SirenComp = MakeComp(LoadSoundSafe(SirenSound, TEXT("Sirene")), TEXT("MTASirenAudio"));
	}
	if (SirenComp)
	{
		SirenComp->SetVolumeMultiplier(0.9f * OutScale);
		UpdateOneShotLoop(SirenComp, bSiren, bSirenPlaying, 0.15f, 0.4f);
	}
}

// ---------------------------------------------------------------------------------------
// Selbsttest
// ---------------------------------------------------------------------------------------

FString UMTAVehicleAudioComponent::RunVehicleAudioSelfTest()
{
	FString Report = TEXT("FAHRZEUGSOUND SELBSTTEST\n");
	int32 Errors = 0;
	auto Check = [&Report, &Errors](bool bOk, const FString& What)
	{
		Report += FString::Printf(TEXT("  [%s] %s\n"), bOk ? TEXT("OK") : TEXT("FEHLER"), *What);
		Errors += bOk ? 0 : 1;
	};

	// 1) Assets ladbar
	TArray<const TCHAR*> Paths;
	for (const TCHAR* P : MTAVehicleAudio::EngineLayerPaths)
	{
		Paths.Add(P);
	}
	Paths.Append({ MTAVehicleAudio::TirePath, MTAVehicleAudio::WindPath, MTAVehicleAudio::HornPath, MTAVehicleAudio::SirenPath,
		MTAVehicleAudio::CollisionPath, MTAVehicleAudio::DoorPath });
	for (const TCHAR* P : Paths)
	{
		const USoundBase* S = Cast<USoundBase>(MTAVehicleAudio::TryLoad(FSoftObjectPath(P)));
		Check(S != nullptr, FString::Printf(TEXT("Sound ladbar: %s"), P));
	}
	const USoundAttenuation* Att = Cast<USoundAttenuation>(MTAVehicleAudio::TryLoad(FSoftObjectPath(MTAVehicleAudio::AttenuationPath)));
	Check(Att != nullptr, FString::Printf(TEXT("Daempfung ladbar: %s"), MTAVehicleAudio::AttenuationPath));

	// 2) Drehzahl-Mischung
	const TArray<float> LayerRPMs = GetDefaultEngineLayerRPMs();
	for (const float TestRPM : { 0.f, 2500.f, 6500.f })
	{
		TArray<float> Gains, Pitches;
		ComputeEngineLayerMix(TestRPM, LayerRPMs, Gains, Pitches);
		float Sum = 0.f;
		float Power = 0.f;
		int32 Loud = 0;
		FString GainText;
		for (int32 I = 0; I < Gains.Num(); ++I)
		{
			Sum += Gains[I];
			Power += Gains[I] * Gains[I];
			Loud += Gains[I] > MTAVehicleAudio::AudibleGain ? 1 : 0;
			GainText += FString::Printf(TEXT("%s%.2f"), I ? TEXT("/") : TEXT(""), Gains[I]);
		}
		const bool bPitchOk = Pitches.Num() == LayerRPMs.Num() && Pitches.Num() > 0
			&& Pitches[0] >= 0.5f && Pitches.Last() <= 2.f;
		Check(Sum > 0.f && Loud >= 1 && Loud <= 2 && FMath::IsNearlyEqual(Power, 1.f, 0.02f) && bPitchOk,
			FString::Printf(TEXT("Mischung %.0f U/min: Gewichte %s, laute Schichten %d, Leistung %.2f"), TestRPM, *GainText, Loud, Power));
	}

	// 3) Komponenten-Budget der Standardkonfiguration (2 Motor + Reifen + Wind + Hupe + Sirene)
	const UMTAVehicleAudioComponent* Defaults = GetDefault<UMTAVehicleAudioComponent>();
	Check(Defaults->EngineLayerSounds.Num() == LayerRPMs.Num() && Defaults->EngineLayerRPMs.Num() == LayerRPMs.Num(),
		FString::Printf(TEXT("Standard-Schichten gesetzt: %d"), Defaults->EngineLayerSounds.Num()));

	Report += FString::Printf(TEXT("FAHRZEUGSOUND ERGEBNIS: %d Fehler"), Errors);
	UE_LOG(LogTemp, Log, TEXT("%s"), *Report);
	return Report;
}
