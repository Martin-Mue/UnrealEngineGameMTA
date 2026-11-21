#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MTAWeatherTypes.h"
#include "MTAAudioVehicleComponent.generated.h"

class UAudioComponent;
class USoundBase;

/** Abstimmung des Fahrzeugklangs (Werte pro Fahrzeugtyp, spaeter aus vehicles.json fuellbar). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAVehicleAudioConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float IdleRPM = 800.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float RedlineRPM = 6800.f;
	/** Glaettung der Eingaben (1/s, hoeher = direkter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float RPMResponse = 14.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float LoadResponse = 8.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float MinPitch = 0.7f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float MaxPitch = 1.7f;
	/** Lautstaerke im Leerlauf ohne Last (0..1), steigt mit Last auf 1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Engine") float IdleLoudness = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tires") float SquealSlipThreshold = 0.25f;
	/** Unterhalb dieser Geschwindigkeit (cm/s) quietscht nichts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tires") float SquealMinSpeed = 300.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind") float WindStartKmh = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Wind") float WindFullKmh = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision") float CollisionMinImpulse = 20000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision") float CollisionMaxImpulse = 400000.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision") float CollisionCooldown = 0.12f;
};

/** Ausgabe pro Frame: reine Zahlen, die ein MetaSound oder ein Mixer verwenden kann. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAVehicleAudioOutput
{
	GENERATED_BODY()

	/** Normierte Drehzahl 0..1 (Leerlauf..Begrenzer). */
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float RPMNorm = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float Load = 0.f;
	/** Equal-Power-Schichten: Leerlauf/Mitte/Hoch (Summe der Quadrate = 1). */
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float EngineGainLow = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float EngineGainMid = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float EngineGainHigh = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float EngineVolume = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float EnginePitch = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float TireSquealGain = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float TireSquealPitch = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float WindGain = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float RainOnBodyGain = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float HornGain = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float SirenGain = 0.f;
	/** Staerke des letzten Aufpralls (klingt ab), 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float CollisionStrength = 0.f;
	/** Daempfung von aussen (Wetter), 0..1. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float WeatherDamping = 0.f;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAVehicleCollisionSound, float, Strength, float, PitchScale);

/**
 * Fahrzeug-Audio als Daten-/Parameterschnittstelle. Das Fahrzeug ruft SetRPM/SetLoad/SetSlip/SetSpeed
 * auf; die Komponente berechnet Schichtgewichte, Pitch, Quietschen, Wind usw. Optional erzeugt sie
 * UAudioComponents (z. B. MetaSound-Quellen) und schiebt die Werte als Parameter hinein. Ohne Assets
 * kompiliert und laeuft sie rein als Parameterquelle.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAVehicleAudioComponent : public UActorComponent, public IMTAWeatherConsumer
{
	GENERATED_BODY()

public:
	UMTAVehicleAudioComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FMTAVehicleAudioConfig Config;

	/** Optionale Quellen (MetaSound oder SoundWave/Cue). Leer = nur Parameter, kein Ton. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> EngineSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> TireSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> WindSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> HornSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> SirenSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> CollisionSound;

	/** Namen der MetaSound-Eingaenge fuer die Motorquelle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamRPM = TEXT("RPM");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamLoad = TEXT("Load");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamGainLow = TEXT("GainLow");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamGainMid = TEXT("GainMid");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamGainHigh = TEXT("GainHigh");

	/** Erzeugt beim Start AudioComponents fuer die gesetzten Quellen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bDriveAudioComponents = true;

	UPROPERTY(BlueprintAssignable, Category = "Audio")
	FMTAVehicleCollisionSound OnCollisionSound;

	// --- Eingaben (vom Fahrzeug jeden Frame) ---
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetRPM(float RPM) { TargetRPM = RPM; }
	/** Last/Gaspedal 0..1. */
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetLoad(float InLoad) { TargetLoad = FMath::Clamp(InLoad, 0.f, 1.f); }
	/** Reifenschlupf 0..1 (Quer- und Laengsschlupf zusammengefasst). */
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetSlip(float InSlip) { Slip = FMath::Clamp(InSlip, 0.f, 1.f); }
	/** Geschwindigkeit in cm/s. */
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetSpeed(float SpeedCmPerSec) { SpeedCms = FMath::Abs(SpeedCmPerSec); }
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetEngineRunning(bool bRunning) { bEngineRunning = bRunning; }
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetHorn(bool bOn) { bHorn = bOn; }
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetSiren(bool bOn) { bSiren = bOn; }
	/** Aufprall melden (Impuls in kg*cm/s wie von Chaos/OnHit geliefert). */
	UFUNCTION(BlueprintCallable, Category = "Audio") void NotifyCollision(float Impulse);

	UFUNCTION(BlueprintPure, Category = "Audio") const FMTAVehicleAudioOutput& GetOutput() const { return Output; }

	/** Equal-Power-Gewichte fuer eine normierte Drehzahl (Low/Mid/High), Summe der Quadrate = 1. */
	UFUNCTION(BlueprintPure, Category = "Audio")
	static void ComputeEngineLayerGains(float RPMNorm, float& Low, float& Mid, float& High);

	// IMTAWeatherConsumer
	virtual void OnWeatherUpdated_Implementation(const FMTAWeatherParams& Params) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	UAudioComponent* MakeComp(const TSoftObjectPtr<USoundBase>& Asset, bool bLoop);
	void PushToAudio();

	FMTAVehicleAudioOutput Output;
	FMTAWeatherParams Weather;
	float TargetRPM = 0.f, SmoothRPM = 0.f;
	float TargetLoad = 0.f, SmoothLoad = 0.f;
	float Slip = 0.f, SpeedCms = 0.f;
	bool bEngineRunning = true, bHorn = false, bSiren = false;
	float LastCollisionTime = -10.f;

	UPROPERTY(Transient) TObjectPtr<UAudioComponent> EngineComp;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> TireComp;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> WindComp;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> HornComp;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> SirenComp;
	TWeakObjectPtr<class AMTAWeatherManager> BoundManager;
	FDelegateHandle ManagerReadyHandle;
};
