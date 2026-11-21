#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/Interface.h"
#include "MTATimeOfDay.generated.h"

class ADirectionalLight;
class ASkyLight;
class AExponentialHeightFog;
class AMTACityGenerator;
class UMaterialParameterCollection;
class UCurveLinearColor;

/**
 * Schnittstelle fuer alles, was die Tageszeit vorgibt (spaeter der GameMode, Story-Skripte,
 * Mehrspieler-Server). AMTATimeOfDay fragt sie ab, statt selbst zu zaehlen.
 */
UINTERFACE(MinimalAPI, BlueprintType)
class UMTATimeProvider : public UInterface
{
	GENERATED_BODY()
};

class MTA6_API IMTATimeProvider
{
	GENERATED_BODY()

public:
	/** Aktuelle Tageszeit in Stunden, 0..24 (z. B. 18.5 = 18:30). */
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category = "MTA|Time")
	float GetTimeOfDayHours() const;
};

/**
 * Tag/Nacht-Steuerung (Portierung von godot/main.gd::_process):
 * Sonnenstand, Sonnen-/Mondlicht, SkyLight, Hoehennebel, globaler Material-Parameter "Night"
 * (Material Parameter Collection) und Laternenlichter des Stadtgenerators.
 * Zeitquelle: (1) TimeProviderActor, (2) GameMode mit IMTATimeProvider, (3) eigene Uhr.
 */
UCLASS(Blueprintable, meta = (DisplayName = "MTA Time Of Day"))
class MTA6_API AMTATimeOfDay : public AActor, public IMTATimeProvider
{
	GENERATED_BODY()

public:
	AMTATimeOfDay();

	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return bPreviewInEditor; }

	/** IMTATimeProvider: liefert die zuletzt angewendete Tageszeit. */
	virtual float GetTimeOfDayHours_Implementation() const override { return TimeOfDayHours; }

	/** Tageszeit setzen und sofort anwenden. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MTA|Time")
	void ApplyTimeOfDay();

	UFUNCTION(BlueprintCallable, Category = "MTA|Time")
	void SetTimeOfDay(float Hours);

	/** 0 = Tag, 1 = volle Nacht (wie "night" im Godot-Prototyp). */
	UFUNCTION(BlueprintPure, Category = "MTA|Time")
	float GetNightAlpha() const { return NightAlpha; }

	/** Fehlende Referenzen (Sonne, SkyLight, Nebel, Generator) in der Welt suchen. */
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "MTA|Time")
	void FindSceneActors();

	// ---------------------------------------------------------------- Zeit

	/** Aktuelle Tageszeit in Stunden (0..24). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Interp, Category = "Time", meta = (ClampMin = "0", ClampMax = "24", UIMin = "0", UIMax = "24"))
	float TimeOfDayHours = 18.f;

	/** Eigene Uhr laufen lassen (nur wenn keine externe Zeitquelle existiert). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time")
	bool bAnimateTime = true;

	/** Echtzeit-Minuten fuer 24 Spielstunden (Godot: 1 h je 20 s = 8 min). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time", meta = (ClampMin = "0.1"))
	float DayLengthMinutes = 8.f;

	/** Optionale externe Zeitquelle (muss IMTATimeProvider implementieren). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time")
	TObjectPtr<AActor> TimeProviderActor;

	/** GameMode als Zeitquelle verwenden, sobald er IMTATimeProvider implementiert. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time")
	bool bUseGameModeTimeProvider = true;

	/** Im Editor-Viewport ohne Play ticken (Zeitraffer-Vorschau). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Time")
	bool bPreviewInEditor = false;

	// ---------------------------------------------------------------- Sonne/Mond

	/** Azimut der Sonnenbahn (Grad); 90 = Sonne geht im Osten (+Y) auf. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun")
	float SunAzimuth = 90.f;

	/** Maximale Sonnenhoehe mittags (Grad). Kestrel Bay liegt in mittleren Breiten. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "10", ClampMax = "90"))
	float MaxSunElevation = 62.f;

	/** Beleuchtungsstaerke der Sonne mittags (Lux, physikalisch; Auto-Exposure regelt nach). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "0"))
	float SunIlluminanceLux = 75000.f;

	/** Mondlicht nachts (Lux). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun", meta = (ClampMin = "0"))
	float MoonIlluminanceLux = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun")
	FLinearColor MoonColor = FLinearColor(0.55f, 0.65f, 1.f);

	/** Optional: Sonnenfarbe ueber normierte Sonnenhoehe (-1..1). Leer = eingebauter Verlauf. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sun")
	TObjectPtr<UCurveLinearColor> SunColorCurve;

	// ---------------------------------------------------------------- Himmel/Nebel

	/** SkyLight-Intensitaet am Tag / in der Nacht (Nacht leicht angehoben, sonst schwarz). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	float SkyLightDayIntensity = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	float SkyLightNightIntensity = 3.f;

	/** Nebeldichte Tag/Nacht (ExponentialHeightFog FogDensity). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	float FogDensityDay = 0.006f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	float FogDensityNight = 0.012f;

	/** Volumetrische Nebel-Extinktion Tag/Nacht. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	float VolumetricExtinctionDay = 0.2f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Sky")
	float VolumetricExtinctionNight = 0.6f;

	// ---------------------------------------------------------------- Referenzen

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "References")
	TObjectPtr<ADirectionalLight> Sun;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "References")
	TObjectPtr<ASkyLight> SkyLight;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "References")
	TObjectPtr<AExponentialHeightFog> HeightFog;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "References")
	TObjectPtr<AMTACityGenerator> CityGenerator;

	/**
	 * Globale Material-Parameter (Scalar "Night", "TimeOfDay", "Wetness").
	 * Leer = /Game/MTA6/Materials/MPC_MTA6_World (von build_materials.py erzeugt).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "References")
	TObjectPtr<UMaterialParameterCollection> WorldParameters;

	/** Intervall fuer die Neuzuordnung der Laternenlichter (s). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "References", meta = (ClampMin = "0.05"))
	float LampUpdateInterval = 0.25f;

	/** Ab dieser Sonnenhoehenaenderung (Grad) wird das SkyLight neu erfasst (nur ohne Echtzeit-Capture). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "References", meta = (ClampMin = "0.5"))
	float SkyRecaptureDegrees = 3.f;

protected:
	virtual void BeginPlay() override;
	virtual void OnConstruction(const FTransform& Transform) override;

private:
	float ResolveHours() const;
	FLinearColor ComputeSunColor(float Elevation01) const;
	UMaterialParameterCollection* ResolveParameterCollection();

	float NightAlpha = 0.f;
	float LampTimer = 0.f;
	float LastRecaptureElevation = -1000.f;
};
