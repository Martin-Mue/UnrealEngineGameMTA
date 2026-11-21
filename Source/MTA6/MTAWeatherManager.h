#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MTAWeatherTypes.h"
#include "MTAWeatherManager.generated.h"

class UFXSystemAsset;
class UFXSystemComponent;
class UMaterialParameterCollection;
class AExponentialHeightFog;
class AMTAWeatherManager;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAWeatherStateChanged, EMTAWeatherState, OldState, EMTAWeatherState, NewState);
/** Blitz: Intensitaet 0..1, Donner kommt nach ThunderDelaySeconds (Schall, ~2,9 s/km). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTALightningStrike, float, Intensity, float, ThunderDelaySeconds);
/** Nativ: wird gefeuert, sobald ein Wettermanager im Level aktiv ist (fuer spaet startende Verbraucher). */
DECLARE_MULTICAST_DELEGATE_OneParam(FMTAWeatherManagerReady, AMTAWeatherManager*);

/** Zielwerte und Dauer eines Wetterzustands aus weather.json. */
struct FMTAWeatherStateDef
{
	FMTAWeatherParams Target;
	float GripFactor = 1.f;
	float DwellMin = 200.f;
	float DwellMax = 400.f;
	float TransitionSeconds = 40.f;
	FName Precip = NAME_None;
};

/**
 * Wetter-Manager: Markov-Kette ueber sieben Zustaende (weather.json), glatte Uebergaenge und
 * Wirkung als Parameter (FMTAWeatherParams). Einmal pro Level platzieren oder per GetOrSpawn
 * erzeugen lassen. Siehe docs/AUDIO_UND_WETTER.md.
 */
UCLASS(BlueprintType)
class MTA6_API AMTAWeatherManager : public AActor
{
	GENERATED_BODY()

public:
	AMTAWeatherManager();

	// --- Konfiguration -----------------------------------------------------------------
	/** JSON relativ zu Content/. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Config")
	FString DataFile = TEXT("Data/weather.json");

	/** Zustaende wechseln automatisch (Markov-Kette). Aus = nur per SetWeather. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Config")
	bool bAutoCycle = true;

	/** Faktor auf die Verweildauer (z. B. 0.2 fuer schnelle Tests). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Config", meta = (ClampMin = "0.01"))
	float DwellScale = 1.f;

	/** != 0 ueberschreibt den Seed aus weather.json. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Config")
	int32 SeedOverride = 0;

	/** Eigene Uhr verwenden, falls niemand SetGameHour aufruft (Tageszeit-System kann sie ueberschreiben). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Config")
	bool bUseOwnClock = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|Config", meta = (ClampMin = "10"))
	float OwnDayLengthSeconds = 1200.f;

	// --- Visuelle Anbindung (alles optional) ----------------------------------------------
	/** Niagara-Regen (Platzhalter). Akzeptiert UNiagaraSystem (abgeleitet von UFXSystemAsset). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX")
	TSoftObjectPtr<UFXSystemAsset> RainSystem;

	/** Niagara-Staub (Platzhalter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX")
	TSoftObjectPtr<UFXSystemAsset> DustSystem;

	/** Material Parameter Collection mit Skalaren: Wetness, RoadRoughness, RainIntensity, FogDensity, WindStrength, DustAmount. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX")
	TObjectPtr<UMaterialParameterCollection> WeatherParameters;

	/** Optionaler Hoehennebel im Level; Dichte wird gesetzt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX")
	TObjectPtr<AExponentialHeightFog> FogActor;

	/** Umrechnung FogDensity (0..1) -> Exponential-Height-Fog-Dichte. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX")
	float FogDensityScale = 0.04f;

	/** Hoehe des Niederschlag-Emitters ueber der Kamera (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weather|FX")
	float PrecipHeightAboveCamera = 900.f;

	// --- Ereignisse ------------------------------------------------------------------------
	UPROPERTY(BlueprintAssignable, Category = "Weather")
	FMTAWeatherStateChanged OnWeatherStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "Weather")
	FMTALightningStrike OnLightning;

	/** Fuer Blueprint-Niederschlag (falls Niagara nicht im Modul eingebunden ist). */
	UFUNCTION(BlueprintImplementableEvent, Category = "Weather")
	void OnPrecipitationChanged(float RainIntensity, float DustAmount);

	// --- API ---------------------------------------------------------------------------------
	UFUNCTION(BlueprintCallable, Category = "Weather")
	bool LoadWeatherData();

	/** Erzwingt einen Zustand. TransitionSeconds < 0 = Standarddauer aus der JSON. */
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void SetWeather(EMTAWeatherState NewState, float TransitionSeconds = -1.f);

	UFUNCTION(BlueprintCallable, Category = "Weather")
	void SetAutoCycle(bool bEnabled) { bAutoCycle = bEnabled; }

	/** Spielstunde 0..24 (vom Tageszeit-System). Beeinflusst die Markov-Gewichte. */
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void SetGameHour(float Hour) { GameHour = FMath::Fmod(FMath::Max(0.f, Hour), 24.f); }

	/** Bezirks-ID (core, saltmarket, ...) fuer die Wetter-Verteilung. */
	UFUNCTION(BlueprintCallable, Category = "Weather")
	void SetDistrict(FName DistrictId) { CurrentDistrict = DistrictId; }

	UFUNCTION(BlueprintPure, Category = "Weather")
	const FMTAWeatherParams& GetParams() const { return Current; }

	UFUNCTION(BlueprintPure, Category = "Weather")
	EMTAWeatherState GetState() const { return Current.State; }

	UFUNCTION(BlueprintPure, Category = "Weather")
	float GetGripMultiplier() const { return Current.GripMultiplier; }

	UFUNCTION(BlueprintCallable, Category = "Weather")
	void RegisterConsumer(UObject* Consumer);

	UFUNCTION(BlueprintCallable, Category = "Weather")
	void UnregisterConsumer(UObject* Consumer);

	/** Sucht den Manager im Level (gecacht). */
	static AMTAWeatherManager* Find(const UObject* WorldContext);
	/** Findet oder erzeugt einen Manager im aktuellen Level. */
	static AMTAWeatherManager* GetOrSpawn(UObject* WorldContext);
	/** Bequemer Zugriff fuer das Fahrzeug: Reifenhaftung (1.0 ohne Manager). */
	UFUNCTION(BlueprintPure, Category = "Weather", meta = (WorldContext = "WorldContext"))
	static float GetGripMultiplierFor(const UObject* WorldContext);

	/** Wird in BeginPlay gefeuert; Verbraucher, die frueher starten, binden sich hier an. */
	static FMTAWeatherManagerReady& OnManagerReady();

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void BeginTransition(EMTAWeatherState NewState, float Seconds);
	EMTAWeatherState PickNextState();
	void ApplyBlend(float Alpha);
	void UpdateWetnessAndDerived(float Dt);
	void UpdateLightning(float Dt);
	void ApplyVisuals();
	void NotifyConsumers();
	void ScheduleDwell();
	void EnsurePrecipitationComponent();

	FMTAWeatherParams Current;
	FMTAWeatherParams BlendFrom;
	TMap<EMTAWeatherState, FMTAWeatherStateDef> Defs;
	float GripFactorFrom = 1.f;
	float GripFactorCurrent = 1.f;
	float MarkovMatrix[7][7];
	struct FBias { float From = 0.f, To = 0.f; float Weights[7]; };
	TArray<FBias> HourBias;
	TMap<FName, TArray<float>> DistrictBias;

	float WetRise = 0.045f, WetDry = 0.004f, WetGripLoss = 0.38f, RoughDry = 0.85f, RoughWet = 0.18f;
	float LightningMin = 4.f, LightningMax = 18.f, ThunderPerKm = 2.9f, LightningMaxKm = 4.f;

	EMTAWeatherState PrevState = EMTAWeatherState::Clear;
	float TransitionElapsed = 0.f;
	float TransitionDuration = 0.f;
	float DwellRemaining = 0.f;
	float Wetness = 0.f;
	float LightningTimer = 8.f;
	float NotifyAccumulator = 0.f;
	float Clock = 0.f;
	float GameHour = 12.f;
	FName CurrentDistrict = TEXT("core");
	bool bLoaded = false;
	FRandomStream Rng;

	TArray<TWeakObjectPtr<UObject>> Consumers;
	UPROPERTY(Transient) TObjectPtr<UFXSystemComponent> PrecipComponent;
	EMTAWeatherState PrecipKindShown = EMTAWeatherState::Count;

	static TWeakObjectPtr<AMTAWeatherManager> CachedManager;
};
