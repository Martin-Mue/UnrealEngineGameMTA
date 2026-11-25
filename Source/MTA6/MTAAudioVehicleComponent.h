#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MTAWeatherTypes.h"
#include "MTAAudioVehicleComponent.generated.h"

class UAudioComponent;
class USoundBase;
class USoundAttenuation;

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
	/** Pitch-Grenzen fuer eine einzelne Motorquelle (MetaSound-Modus ohne Drehzahl-Schichten). */
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
	/** Aufprallgeschwindigkeit (cm/s entlang der Trefferrichtung) fuer leisesten / lautesten Schlag. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision") float CollisionMinSpeed = 250.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision") float CollisionMaxSpeed = 2500.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Collision") float CollisionCooldown = 0.12f;
};

/** Ausgabe pro Frame: reine Zahlen, die ein MetaSound oder ein Mixer verwenden kann. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAVehicleAudioOutput
{
	GENERATED_BODY()

	/** Normierte Drehzahl 0..1 (Leerlauf..Begrenzer). */
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float RPMNorm = 0.f;
	/** Geglaettete Drehzahl in U/min. */
	UPROPERTY(BlueprintReadOnly, Category = "Audio") float RPM = 0.f;
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
 * Fahrzeug-Audio. Das Fahrzeug ruft SetRPM/SetLoad/SetSlip/SetSpeed auf; die Komponente berechnet
 * Schichtgewichte, Pitch, Quietschen, Wind usw. und steuert damit hoechstens 6 UAudioComponents:
 *   2 x Motor (benachbarte Drehzahl-Loops, Lautstaerke-Ueberblendung + Pitch nach Drehzahl),
 *   Reifen, Wind, Hupe, Sirene (Sirene erst bei Bedarf). Aufprall = Einzelklang ohne Komponente.
 * Standard-Assets liegen unter /Game/MTA6/Audio/Vehicle (tools/ue_audio/import_vehicle_sounds.py).
 * Fehlende Assets ergeben Stille und eine Warnung (LogTemp), nie einen Absturz.
 * Klang laeuft nur, solange das Fahrzeug vom Spieler gesteuert wird (bOnlyWhenPlayerControlled).
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAVehicleAudioComponent : public UActorComponent, public IMTAWeatherConsumer
{
	GENERATED_BODY()

public:
	UMTAVehicleAudioComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	FMTAVehicleAudioConfig Config;

	/** Motor-Loops, aufsteigend nach Drehzahl; EngineLayerRPMs nennt die Aufnahmedrehzahl je Loop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TArray<TSoftObjectPtr<USoundBase>> EngineLayerSounds;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TArray<float> EngineLayerRPMs;

	/** Optionale einzelne Motorquelle (z. B. MetaSound); nur genutzt, wenn keine Drehzahl-Schicht ladbar ist. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> EngineSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> TireSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> WindSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> HornSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> SirenSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundBase> CollisionSound;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|Sounds") TSoftObjectPtr<USoundAttenuation> Attenuation;

	/** Gesamtlautstaerke aller Fahrzeugklaenge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio", meta = (ClampMin = "0", ClampMax = "2"))
	float MasterVolume = 1.f;

	/** Namen der MetaSound-Eingaenge fuer die einzelne Motorquelle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamRPM = TEXT("RPM");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamLoad = TEXT("Load");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamGainLow = TEXT("GainLow");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamGainMid = TEXT("GainMid");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio|MetaSound") FName ParamGainHigh = TEXT("GainHigh");

	/** Erzeugt bei Aktivierung AudioComponents fuer die gesetzten Quellen (aus = reine Parameterquelle). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bDriveAudioComponents = true;

	/** Nur hoerbar, solange ein Spieler das besitzende Fahrzeug steuert. Aus = immer aktiv. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Audio")
	bool bOnlyWhenPlayerControlled = true;

	UPROPERTY(BlueprintAssignable, Category = "Audio")
	FMTAVehicleCollisionSound OnCollisionSound;

	// --- Eingaben (vom Fahrzeug jeden Frame) ---
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetRPM(float InRPM) { TargetRPM = FMath::Max(0.f, InRPM); }
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
	/** Aufprall melden ueber die Aufprallgeschwindigkeit (cm/s entlang der Trefferrichtung). */
	UFUNCTION(BlueprintCallable, Category = "Audio") void NotifyImpactSpeed(float ImpactSpeedCms);

	/** Klang ein-/ausschalten (legt beim ersten Einschalten die AudioComponents an). */
	UFUNCTION(BlueprintCallable, Category = "Audio") void SetAudioActive(bool bActive);
	UFUNCTION(BlueprintPure, Category = "Audio") bool IsAudioActive() const { return bAudioActive; }

	UFUNCTION(BlueprintPure, Category = "Audio") const FMTAVehicleAudioOutput& GetOutput() const { return Output; }

	/** Anzahl der aktuell angelegten AudioComponents (hoechstens 6). */
	UFUNCTION(BlueprintPure, Category = "Audio") int32 GetNumAudioComponents() const;

	/** Equal-Power-Gewichte fuer eine normierte Drehzahl (Low/Mid/High), Summe der Quadrate = 1. */
	UFUNCTION(BlueprintPure, Category = "Audio")
	static void ComputeEngineLayerGains(float RPMNorm, float& Low, float& Mid, float& High);

	/**
	 * Drehzahl-Mischung: Equal-Power-Ueberblendung zwischen den zwei benachbarten Loops (aufsteigende
	 * LayerRPMs). Unter/ueber dem Bereich spielt nur der erste/letzte Loop. OutPitches = RPM / LayerRPM (0.5..2).
	 */
	UFUNCTION(BlueprintPure, Category = "Audio")
	static void ComputeEngineLayerMix(float InRPM, const TArray<float>& LayerRPMs, TArray<float>& OutGains, TArray<float>& OutPitches);

	/** Standard-Drehzahlen der mitgelieferten Motor-Loops (800/2000/3500/5500 U/min). */
	static TArray<float> GetDefaultEngineLayerRPMs();

	/**
	 * Selbsttest: alle Standard-Asset-Pfade ladbar, Drehzahl-Mischung fuer 0/2500/6500 U/min plausibel.
	 * Bericht endet mit "FAHRZEUGSOUND ERGEBNIS: n Fehler".
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio|Test")
	static FString RunVehicleAudioSelfTest();

	// IMTAWeatherConsumer
	virtual void OnWeatherUpdated_Implementation(const FMTAWeatherParams& Params) override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float Dt, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

private:
	USoundBase* LoadSoundSafe(const TSoftObjectPtr<USoundBase>& Asset, const TCHAR* What);
	UAudioComponent* MakeComp(USoundBase* Sound, const TCHAR* CompName);
	void CreateAudioComponents();
	void StopAllAudio(float FadeSeconds);
	void UpdateEngineSlots(float OutScale);
	void UpdateOneShotLoop(UAudioComponent* Comp, bool bWanted, bool& bPlayingFlag, float FadeIn, float FadeOutSeconds);
	void PlayCollision(float Strength);
	void PushToAudio();
	UAudioComponent* GetEngineSlot(int32 Slot) const;

	FMTAVehicleAudioOutput Output;
	FMTAWeatherParams Weather;
	float TargetRPM = 0.f, SmoothRPM = 0.f;
	float TargetLoad = 0.f, SmoothLoad = 0.f;
	float Slip = 0.f, SpeedCms = 0.f;
	bool bEngineRunning = true, bHorn = false, bSiren = false;
	float LastCollisionTime = -10.f;

	bool bAudioActive = false;
	bool bComponentsCreated = false;
	bool bHornPlaying = false;
	bool bSirenPlaying = false;
	bool bSingleEngineSource = false;

	/** Geladene Motor-Loops (Index = Schicht) und ihre Drehzahlen. */
	UPROPERTY(Transient) TArray<TObjectPtr<USoundBase>> LoadedEngineLayers;
	TArray<float> LoadedLayerRPMs;
	/** Welche Schicht gerade in Slot 0/1 liegt (-1 = keine). */
	int32 SlotLayer[2] = { -1, -1 };
	TArray<float> MixGains;
	TArray<float> MixPitches;

	UPROPERTY(Transient) TObjectPtr<UAudioComponent> EngineSlotA;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> EngineSlotB;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> TireComp;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> WindComp;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> HornComp;
	UPROPERTY(Transient) TObjectPtr<UAudioComponent> SirenComp;
	UPROPERTY(Transient) TObjectPtr<USoundBase> LoadedCollision;
	UPROPERTY(Transient) TObjectPtr<USoundAttenuation> LoadedAttenuation;

	TWeakObjectPtr<class AMTAWeatherManager> BoundManager;
	FDelegateHandle ManagerReadyHandle;
};
