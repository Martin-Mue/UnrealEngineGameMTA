#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MTAChaosTypes.h"
#include "MTAStealthComponent.generated.h"

class AMTAStealthHideout;
class AMTATimeOfDay;
class ULocalLightComponent;
class UMTAChaosSubsystem;

/** Fluchtzustand des Spielers (fuer HUD und Musik). */
UENUM(BlueprintType)
enum class EMTAEscapeState : uint8
{
	Clear     UMETA(DisplayName = "Unauffaellig"),
	Wanted    UMETA(DisplayName = "Gesucht"),
	Spotted   UMETA(DisplayName = "Gesehen"),
	LyingLow  UMETA(DisplayName = "Untergetaucht")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAOutfitChangedSignature, FName, OldOutfit, FName, NewOutfit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAPlateChangedSignature, AActor*, Vehicle, FName, NewPlate);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTANoiseSignature, FVector, Location, float, Loudness);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAEscapeStateSignature, EMTAEscapeState, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAHideoutSignature, AMTAStealthHideout*, Hideout);

/**
 * Tarnung und Flucht (auf der Spielerfigur).
 * Sichtbarkeit 0..1 aus Umgebungslicht (Tageszeit), lokalen Lichtern, Hocke, Tempo und Verstecken;
 * Geraeuschpegel 0..1 aus Bewegung und Ereignissen. Zeugen und Aegis-Kameras fragen beides ueber
 * die statischen Helfer ab. Untertauchen in Verstecken senkt Aegis-Profil und Bezirks-Heat;
 * Kleidungs- und Kennzeichenwechsel sind Datenhooks fuer Inventar/Garage.
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAStealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMTAStealthComponent();

	// --- Statische Abfragen fuer Zeugen/Kameras ---------------------------------------

	/** Sucht die Komponente am Aktor oder an angehaengten Aktoren (Figur im Fahrzeug). */
	static UMTAStealthComponent* FindStealthComponent(const AActor* Actor);

	/** Sichtbarkeit 0..1 eines Subjekts (1 = ohne Tarnkomponente). */
	static float GetSubjectVisibility(const AActor* Subject);

	/** Faktor auf die Aegis-Erfassung (Kennzeichen-Stoerer im Fahrzeug, Maske zu Fuss). */
	static float GetCaptureModifier(const AActor* Subject, bool bAsVehicle);

	// --- Licht und Sichtbarkeit -------------------------------------------------------

	/** Platzhalter-Tageszeit (h), falls kein AMTATimeOfDay in der Welt liegt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Light")
	float FallbackTimeOfDayHours = 13.f;

	/** Umgebungslicht bei Tag bzw. tiefer Nacht (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Light")
	float DayAmbient = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Light")
	float NightAmbient = 0.2f;

	/** Lichtstaerke einer lokalen Lampe, die direkt daneben volle Helligkeit ergibt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Light")
	float ReferenceLightIntensity = 5000.f;

	/** Nur Lampen in diesem Radius (cm) werden betrachtet. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Light")
	float LightSearchRadius = 3000.f;

	/** Max. Anzahl Lampen mit Verdeckungs-Trace pro Messung. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Light")
	int32 MaxLightTraces = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Light")
	float LightSampleInterval = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth")
	float CrouchVisibilityFactor = 0.55f;

	/** Sichtbarkeitszuschlag beim Sprinten. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth")
	float SprintVisibilityBonus = 0.2f;

	/** Sichtbarkeit in einem Fahrzeug (Kennzeichen sichtbar, Insasse kaum). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth")
	float InVehicleVisibility = 0.85f;

	// --- Geraeusch ----------------------------------------------------------------------

	/** Abbau von Geraeuschspitzen pro Sekunde. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Noise")
	float NoiseDecay = 0.8f;

	/** Ab dieser Lautstaerke wird OnNoise gefeuert (Polizei-KI: Geraeusch untersuchen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Noise")
	float NoiseEventThreshold = 0.5f;

	// --- Verkleidung (Datenhooks) -------------------------------------------------------

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Disguise")
	FName OutfitId = TEXT("outfit_default");

	/** Maske/Kapuze: Gesichtserkennung erschwert. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Disguise")
	bool bFaceCovered = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Disguise", meta = (ClampMin = "0", ClampMax = "1"))
	float FaceCoveredCaptureFactor = 0.3f;

	/** Kennzeichen-Stoerer (Modul): 1 = keiner, 0.5 = halbiert die Lesechance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Disguise", meta = (ClampMin = "0", ClampMax = "1"))
	float PlateJammerFactor = 1.f;

	/** Aegis-Profil-Senkung bei Kleidungs- bzw. Kennzeichenwechsel (nur wenn das Alte bekannt war). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Disguise")
	float OutfitChangeAegisReduction = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Disguise")
	float PlateSwapAegisReduction = 8.f;

	// --- Untertauchen ------------------------------------------------------------------

	/** Mindestzeit (s) ohne Sichtung, bevor Untertauchen wirkt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Stealth|Hideout")
	float MinUnseenTimeToLieLow = 5.f;

	// --- Zustand (lesend) ---------------------------------------------------------------

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Stealth")
	float Visibility = 1.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Stealth")
	float LightLevel = 1.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Stealth")
	float NoiseLevel = 0.f;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Stealth")
	EMTAEscapeState EscapeState = EMTAEscapeState::Clear;

	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "MTA6|Stealth")
	TObjectPtr<AMTAStealthHideout> CurrentHideout;

	// --- Ereignisse ----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Stealth")
	FMTAOutfitChangedSignature OnOutfitChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Stealth")
	FMTAPlateChangedSignature OnPlateChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Stealth")
	FMTANoiseSignature OnNoise;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Stealth")
	FMTAEscapeStateSignature OnEscapeStateChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Stealth")
	FMTAHideoutSignature OnEnteredHideout;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Stealth")
	FMTAHideoutSignature OnLeftHideout;

	// --- API -----------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Stealth")
	FName GetOutfitId() const { return OutfitId; }

	/** Geraeuschspitze (Landung 0.3, Aufbrechen 0.7, Unfall 1.0). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Stealth")
	void ReportNoise(float Loudness);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Stealth|Disguise")
	void ChangeOutfit(FName NewOutfitId);

	/** Kennzeichen eines Fahrzeugs tauschen (leer = zufaellig). Gibt das neue Kennzeichen zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Stealth|Disguise")
	FName SwapVehiclePlate(AActor* Vehicle, FName NewPlate = NAME_None);

	void EnterHideout(AMTAStealthHideout* Hideout);
	void LeaveHideout(AMTAStealthHideout* Hideout);

	UFUNCTION(BlueprintPure, Category = "MTA6|Stealth")
	bool IsInHideout() const { return CurrentHideout != nullptr; }

	/** Aktuelle Tageszeit (AMTATimeOfDay oder Platzhalter). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Stealth")
	float GetNightAlpha() const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	UFUNCTION()
	void HandleSighting(AActor* Subject, FVector Location, EMTASightingSource Source);

	void SampleLight();
	void RefreshLightCache();
	void UpdateEscapeState(float DeltaTime);
	UMTAChaosSubsystem* GetChaos() const;
	FName GetCurrentDistrict() const;

	TArray<TWeakObjectPtr<ULocalLightComponent>> CachedLights;
	TWeakObjectPtr<AMTATimeOfDay> TimeOfDayActor;
	float LightSampleTimer = 0.f;
	float LightCacheTimer = 0.f;
	float LastSightedTime = -1000.f;
	float LastOutfitChangeTime = -1000.f;
	float MovementNoise = 0.f;
	float EventNoise = 0.f;
};
