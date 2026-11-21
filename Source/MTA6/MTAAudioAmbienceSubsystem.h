#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MTAAudioTypes.h"
#include "MTAWeatherTypes.h"
#include "MTAAudioAmbienceSubsystem.generated.h"

class UAudioComponent;
class AMTAWeatherManager;

/** Hall-/Reflexionsparameter (nach Strassenbreite), z. B. fuer Submix-Reverb-Einstellungen. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAReverbParams
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") float DecaySeconds = 1.5f;
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") float Wet = 0.2f;
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") float PreDelayMs = 30.f;
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") float ReflectionGain = 0.4f;
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") float HighCutHz = 9000.f;
};

/** Durchsage des Ueberwachungsnetzes Aegis bzw. Polizeifunk (Text + Sprecher; Vertonung spaeter). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAAegisLine
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") FName Id;
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") FString Speaker;
	/** Announcement, Drone oder PoliceRadio. */
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") FName Category;
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") FString Text;
	UPROPERTY(BlueprintReadOnly, Category = "Ambience") float DurationSec = 5.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAAegisLineDelegate, const FMTAAegisLine&, Line);

/**
 * Umgebungs-Audio: mischt Layer (Content/Data/ambience.json) nach Bezirk x Tageszeit x Wetter,
 * berechnet Hall nach Strassenbreite und feuert Aegis-/Polizeifunk-Durchsagen.
 * Reine Parameter (GetLayerGains/GetReverb); optional werden die Layer selbst abgespielt.
 */
UCLASS()
class MTA6_API UMTAAmbienceSubsystem : public UTickableWorldSubsystem, public IMTAWeatherConsumer
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UMTAAmbienceSubsystem, STATGROUP_Tickables); }

	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	bool LoadAmbienceData(const FString& RelativePath = TEXT("Data/ambience.json"));

	/** Alles auf einmal setzen. StreetWidthMeters <= 0: Freifeld. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetListenerContext(EMTADistrict District, float GameHour, float StreetWidthMeters, bool bInTunnel);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetDistrict(EMTADistrict District);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetGameHour(float Hour);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetStreetWidth(float Meters);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetInTunnel(bool bTunnel);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetWantedLevel(int32 Level);
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetStoryFlag(FName Flag, bool bSet = true);

	/** Geglaettete Layer-Gewichte (0..1, ohne baseGainDb). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Ambience")
	float GetLayerGain(FName Layer) const;
	UFUNCTION(BlueprintPure, Category = "MTA6|Ambience")
	const TMap<FName, float>& GetLayerGains() const { return Gains; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Ambience")
	const FMTAReverbParams& GetReverb() const { return Reverb; }
	UFUNCTION(BlueprintPure, Category = "MTA6|Ambience")
	const FMTAAudioContext& GetContext() const { return Context; }

	/** Layer selbst abspielen (setzt importierte Assets /Game/Audio/Generated/amb_*.amb_* voraus). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void SetPlaybackEnabled(bool bEnabled);

	/** Donner nach Blitz: spielt amb_thunder mit Verzoegerung (vom WeatherManager-Event aus aufrufbar). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Ambience")
	void PlayThunder(float Intensity, float DelaySeconds);

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Ambience")
	FMTAAegisLineDelegate OnAegisLine;

	// IMTAWeatherConsumer
	virtual void OnWeatherUpdated_Implementation(const FMTAWeatherParams& Params) override;

private:
	struct FLayerDef { FString File; bool bLoop = true; float BaseGain = 1.f; bool bDamped = true; };
	struct FMod { float Mul = 1.f; float Add = 0.f; };
	struct FAegisEntry { FName Id; FString Speaker; FName Category; FString Text; FMTAAudioCondition When; };

	void BindWeather();
	void ComputeTargetGains(TMap<FName, float>& Out) const;
	void ComputeBandWeights(float Hour, float Out[4]) const;
	void ComputeGainsForState(EMTAWeatherState State, const float Band[4], TMap<FName, float>& Out) const;
	FMTAReverbParams ComputeReverbTarget() const;
	void TickAegis(float Dt);
	void FireLine(const FAegisEntry& E);
	void UpdatePlayback();

	TMap<FName, FLayerDef> Layers;
	// Bezirk -> Band(0..3: dawn, day, dusk, night) -> Layer -> Gewicht
	TMap<FName, TArray<TMap<FName, float>>> DistrictBands;
	// Wetterzustand -> Layer -> Modifikator
	TMap<FName, TMap<FName, FMod>> WeatherMods;
	float BandFrom[4] = { 5, 8, 17, 21 };
	float BandTo[4] = { 8, 17, 21, 5 };

	struct FReverbPoint { float Width, Decay, Wet, PreDelay, Refl, HighCut; };
	TArray<FReverbPoint> ReverbTable;
	FMTAReverbParams TunnelReverb, OpenReverb;
	TMap<FName, float> DistrictWetMul;

	TMap<FName, float> AegisDensity;
	TArray<FAegisEntry> AegisLines, PoliceLines;
	float AegisMinInterval = 45.f, AegisMaxInterval = 140.f, PoliceInterval = 12.f;
	float AegisTimer = 30.f, PoliceTimer = 8.f;

	TMap<FName, float> Gains;
	FMTAReverbParams Reverb;
	FMTAAudioContext Context;
	FMTAWeatherParams Weather;
	float StreetWidth = 16.f;
	bool bTunnel = false;
	float Smoothing = 3.f;
	FString AudioPath = TEXT("/Game/Audio/Generated/");
	bool bLoaded = false;
	bool bPlayback = false;
	float BindTimer = 0.f;
	TWeakObjectPtr<AMTAWeatherManager> BoundManager;
	FDelegateHandle ManagerReadyHandle;
	FRandomStream Rng;

	UPROPERTY() TMap<FName, TObjectPtr<UAudioComponent>> LayerComps;
};
