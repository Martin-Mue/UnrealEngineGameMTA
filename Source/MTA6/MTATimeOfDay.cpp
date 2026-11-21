#include "MTATimeOfDay.h"

#include "MTACityGenerator.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/SkyLightComponent.h"
#include "Curves/CurveLinearColor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

DEFINE_LOG_CATEGORY_STATIC(LogMTATime, Log, All);

namespace
{
	const TCHAR* const DefaultWorldParameters = TEXT("/Game/MTA6/Materials/MPC_MTA6_World.MPC_MTA6_World");
	const FName NightParam(TEXT("Night"));
	const FName TimeParam(TEXT("TimeOfDay"));
}

AMTATimeOfDay::AMTATimeOfDay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
	// Licht/Nebel nach der Spiellogik aktualisieren (Kameraposition ist dann aktuell).
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMTATimeOfDay::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	// Im Editor: Regler "TimeOfDayHours" zeigt sofort das Ergebnis.
	ApplyTimeOfDay();
}

void AMTATimeOfDay::BeginPlay()
{
	Super::BeginPlay();
	FindSceneActors();
	if (!ResolveParameterCollection())
	{
		UE_LOG(LogMTATime, Warning, TEXT("%s: MPC_MTA6_World fehlt - Fenster/Laternen reagieren nicht auf die Nacht. tools/ue/build_materials.py ausfuehren."), *GetName());
	}
	ApplyTimeOfDay();
}

void AMTATimeOfDay::FindSceneActors()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	if (!Sun)
	{
		TActorIterator<ADirectionalLight> It(World);
		Sun = It ? *It : nullptr;
	}
	if (!SkyLight)
	{
		TActorIterator<ASkyLight> It(World);
		SkyLight = It ? *It : nullptr;
	}
	if (!HeightFog)
	{
		TActorIterator<AExponentialHeightFog> It(World);
		HeightFog = It ? *It : nullptr;
	}
	if (!CityGenerator)
	{
		TActorIterator<AMTACityGenerator> It(World);
		CityGenerator = It ? *It : nullptr;
	}
}

UMaterialParameterCollection* AMTATimeOfDay::ResolveParameterCollection()
{
	if (!WorldParameters)
	{
		WorldParameters = LoadObject<UMaterialParameterCollection>(nullptr, DefaultWorldParameters, nullptr, LOAD_NoWarn | LOAD_Quiet);
	}
	return WorldParameters;
}

float AMTATimeOfDay::ResolveHours() const
{
	// 1) Ausdruecklich gesetzte Zeitquelle
	if (TimeProviderActor && TimeProviderActor != this && TimeProviderActor->Implements<UMTATimeProvider>())
	{
		return IMTATimeProvider::Execute_GetTimeOfDayHours(TimeProviderActor);
	}
	// 2) GameMode (nur auf dem Server/Standalone vorhanden; Clients nutzen spaeter die replizierte Zeit)
	if (bUseGameModeTimeProvider)
	{
		if (const UWorld* World = GetWorld())
		{
			AGameModeBase* GameMode = World->GetAuthGameMode();
			if (GameMode && GameMode->Implements<UMTATimeProvider>())
			{
				return IMTATimeProvider::Execute_GetTimeOfDayHours(GameMode);
			}
		}
	}
	return -1.f;
}

void AMTATimeOfDay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	const float External = ResolveHours();
	if (External >= 0.f)
	{
		TimeOfDayHours = FMath::Fmod(External, 24.f);
	}
	else if (bAnimateTime)
	{
		const float HoursPerSecond = 24.f / (DayLengthMinutes * 60.f);
		TimeOfDayHours = FMath::Fmod(TimeOfDayHours + DeltaSeconds * HoursPerSecond, 24.f);
	}
	ApplyTimeOfDay();

	// Laternenlichter (Pool) zu den naechsten Laternen um die Kamera schieben.
	LampTimer -= DeltaSeconds;
	if (LampTimer <= 0.f && CityGenerator)
	{
		LampTimer = LampUpdateInterval;
		const UWorld* World = GetWorld();
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		if (PC && PC->PlayerCameraManager)
		{
			CityGenerator->UpdateLampLights(PC->PlayerCameraManager->GetCameraLocation(), NightAlpha);
		}
	}
}

void AMTATimeOfDay::SetTimeOfDay(float Hours)
{
	TimeOfDayHours = FMath::Fmod(FMath::Max(0.f, Hours), 24.f);
	ApplyTimeOfDay();
}

FLinearColor AMTATimeOfDay::ComputeSunColor(float Elevation01) const
{
	if (SunColorCurve)
	{
		return SunColorCurve->GetLinearColorValue(Elevation01);
	}
	// Warm am Horizont, fast weiss mittags (die SkyAtmosphere faerbt zusaetzlich physikalisch).
	const float T = FMath::Clamp(Elevation01 / 0.4f, 0.f, 1.f);
	return FMath::Lerp(FLinearColor(1.f, 0.72f, 0.5f), FLinearColor(1.f, 0.97f, 0.93f), T);
}

void AMTATimeOfDay::ApplyTimeOfDay()
{
	// Godot: a = (hour - 6) / 24 * TAU, elev = sin(a), day = clamp((elev + 0.15) / 0.45)
	const float A = (TimeOfDayHours - 6.f) / 24.f * UE_TWO_PI;
	const float Elev = FMath::Sin(A);
	const float Day = FMath::Clamp((Elev + 0.15f) / 0.45f, 0.f, 1.f);
	NightAlpha = 1.f - Day;

	// Sonnenrichtung: Azimut laeuft von Ost (6 Uhr) ueber Sued (12 Uhr) nach West (18 Uhr).
	// +X = Nord, +Y = Ost. S = Richtung vom Boden zur Sonne.
	const float AzimuthDeg = SunAzimuth + FMath::RadiansToDegrees(A);
	const float ElevationDeg = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Elev, -1.f, 1.f))) * (MaxSunElevation / 90.f);
	const FVector SunDir = FRotator(ElevationDeg, AzimuthDeg, 0.f).Vector();

	if (Sun)
	{
		if (ULightComponent* Light = Sun->GetLightComponent())
		{
			const bool bSunUp = Elev > -0.02f;
			if (bSunUp)
			{
				// Licht faellt von der Sonne weg: Vorwaertsvektor = -S. Am Horizont weich ausblenden.
				Sun->SetActorRotation((-SunDir).Rotation());
				const float Fade = FMath::SmoothStep(-0.02f, 0.06f, Elev);
				Light->SetIntensity(SunIlluminanceLux * Fade);
				Light->SetLightColor(ComputeSunColor(Elev));
			}
			else
			{
				// Mond steht der Sonne gegenueber: Vorwaertsvektor = +S.
				Sun->SetActorRotation(SunDir.Rotation());
				const float Fade = FMath::SmoothStep(0.02f, 0.12f, -Elev);
				Light->SetIntensity(MoonIlluminanceLux * Fade);
				Light->SetLightColor(MoonColor);
			}
		}
	}

	if (SkyLight)
	{
		if (USkyLightComponent* Sky = SkyLight->GetLightComponent())
		{
			Sky->SetIntensity(FMath::Lerp(SkyLightDayIntensity, SkyLightNightIntensity, NightAlpha));
			// Ohne Echtzeit-Capture muss der Himmel bei Sonnenbewegung neu erfasst werden.
			if (!Sky->bRealTimeCapture && FMath::Abs(ElevationDeg - LastRecaptureElevation) > SkyRecaptureDegrees)
			{
				LastRecaptureElevation = ElevationDeg;
				Sky->RecaptureSky();
			}
		}
	}

	if (HeightFog)
	{
		if (UExponentialHeightFogComponent* Fog = HeightFog->GetComponent())
		{
			Fog->SetFogDensity(FMath::Lerp(FogDensityDay, FogDensityNight, NightAlpha));
			Fog->SetVolumetricFogExtinctionScale(FMath::Lerp(VolumetricExtinctionDay, VolumetricExtinctionNight, NightAlpha));
		}
	}

	// Globale Materialparameter: Fenster, Laternenkoepfe, Leuchtkrone.
	if (UMaterialParameterCollection* MPC = ResolveParameterCollection())
	{
		if (GetWorld())
		{
			UKismetMaterialLibrary::SetScalarParameterValue(this, MPC, NightParam, NightAlpha);
			UKismetMaterialLibrary::SetScalarParameterValue(this, MPC, TimeParam, TimeOfDayHours);
		}
	}
}
