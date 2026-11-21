#include "MTAAudioVehicleComponent.h"
#include "MTAWeatherManager.h"
#include "Components/AudioComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

UMTAVehicleAudioComponent::UMTAVehicleAudioComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
}

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

void UMTAVehicleAudioComponent::OnWeatherUpdated_Implementation(const FMTAWeatherParams& Params)
{
	Weather = Params;
}

UAudioComponent* UMTAVehicleAudioComponent::MakeComp(const TSoftObjectPtr<USoundBase>& Asset, bool bLoop)
{
	AActor* Owner = GetOwner();
	if (!Owner || Asset.IsNull())
	{
		return nullptr;
	}
	USoundBase* Sound = Asset.LoadSynchronous();
	if (!Sound)
	{
		return nullptr;
	}
	UAudioComponent* C = NewObject<UAudioComponent>(Owner);
	C->bAutoActivate = false;
	C->bAutoDestroy = false;
	C->SetSound(Sound);
	C->SetVolumeMultiplier(0.f);
	C->RegisterComponent();
	if (Owner->GetRootComponent())
	{
		C->AttachToComponent(Owner->GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);
	}
	C->Play();
	return C;
}

void UMTAVehicleAudioComponent::BeginPlay()
{
	Super::BeginPlay();
	if (bDriveAudioComponents)
	{
		EngineComp = MakeComp(EngineSound, true);
		TireComp = MakeComp(TireSound, true);
		WindComp = MakeComp(WindSound, true);
		HornComp = MakeComp(HornSound, true);
		SirenComp = MakeComp(SirenSound, true);
	}
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
	for (UAudioComponent* C : { EngineComp.Get(), TireComp.Get(), WindComp.Get(), HornComp.Get(), SirenComp.Get() })
	{
		if (C)
		{
			C->Stop();
			C->DestroyComponent();
		}
	}
	Super::EndPlay(Reason);
}

void UMTAVehicleAudioComponent::NotifyCollision(float Impulse)
{
	const UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;
	if (Impulse < Config.CollisionMinImpulse || Now - LastCollisionTime < Config.CollisionCooldown)
	{
		return;
	}
	LastCollisionTime = Now;
	const float Strength = FMath::Clamp((Impulse - Config.CollisionMinImpulse) / FMath::Max(1.f, Config.CollisionMaxImpulse - Config.CollisionMinImpulse), 0.f, 1.f);
	Output.CollisionStrength = FMath::Max(Output.CollisionStrength, Strength);
	const float Pitch = FMath::Lerp(1.15f, 0.8f, Strength); // schwere Treffer klingen tiefer
	OnCollisionSound.Broadcast(Strength, Pitch);
	if (!CollisionSound.IsNull() && GetOwner())
	{
		if (USoundBase* S = CollisionSound.LoadSynchronous())
		{
			UGameplayStatics::PlaySoundAtLocation(this, S, GetOwner()->GetActorLocation(), FMath::Lerp(0.3f, 1.f, Strength) * (1.f - Weather.SoundDamping * 0.5f), Pitch);
		}
	}
}

void UMTAVehicleAudioComponent::TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(Dt, TickType, ThisTick);

	SmoothRPM = FMath::FInterpTo(SmoothRPM, bEngineRunning ? TargetRPM : 0.f, Dt, Config.RPMResponse);
	SmoothLoad = FMath::FInterpTo(SmoothLoad, TargetLoad, Dt, Config.LoadResponse);

	FMTAVehicleAudioOutput& O = Output;
	O.WeatherDamping = Weather.SoundDamping;
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
	O.TireSquealGain = FMath::SmoothStep(0.f, 1.f, SlipAmt) * MoveAmt;
	O.TireSquealPitch = FMath::Lerp(0.8f, 1.3f, Slip);

	// Wind nach Tempo (cm/s -> km/h = *0.036) und Windstaerke
	const float Kmh = SpeedCms * 0.036f;
	const float Wt = FMath::Clamp((Kmh - Config.WindStartKmh) / FMath::Max(1.f, Config.WindFullKmh - Config.WindStartKmh), 0.f, 1.f);
	O.WindGain = FMath::Clamp(FMath::Pow(Wt, 1.5f) + Weather.WindGust * 0.25f, 0.f, 1.f);
	O.RainOnBodyGain = Weather.RainIntensity * FMath::Clamp(0.3f + Kmh / 120.f, 0.f, 1.f);

	O.HornGain = FMath::FInterpTo(O.HornGain, bHorn ? 1.f : 0.f, Dt, 30.f);
	O.SirenGain = FMath::FInterpTo(O.SirenGain, bSiren ? 1.f : 0.f, Dt, 10.f);
	O.CollisionStrength = FMath::FInterpTo(O.CollisionStrength, 0.f, Dt, 3.f);

	PushToAudio();
}

void UMTAVehicleAudioComponent::PushToAudio()
{
	const float Out = 1.f - Output.WeatherDamping * 0.5f;
	if (EngineComp)
	{
		EngineComp->SetFloatParameter(ParamRPM, Output.RPMNorm);
		EngineComp->SetFloatParameter(ParamLoad, Output.Load);
		EngineComp->SetFloatParameter(ParamGainLow, Output.EngineGainLow);
		EngineComp->SetFloatParameter(ParamGainMid, Output.EngineGainMid);
		EngineComp->SetFloatParameter(ParamGainHigh, Output.EngineGainHigh);
		EngineComp->SetVolumeMultiplier(Output.EngineVolume * Out);
		EngineComp->SetPitchMultiplier(Output.EnginePitch);
	}
	if (TireComp)
	{
		TireComp->SetVolumeMultiplier(Output.TireSquealGain * Out);
		TireComp->SetPitchMultiplier(Output.TireSquealPitch);
	}
	if (WindComp)
	{
		WindComp->SetVolumeMultiplier(Output.WindGain);
	}
	if (HornComp)
	{
		HornComp->SetVolumeMultiplier(Output.HornGain * Out);
	}
	if (SirenComp)
	{
		SirenComp->SetVolumeMultiplier(Output.SirenGain * Out);
	}
}
