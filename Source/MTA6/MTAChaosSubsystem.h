#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectKey.h"
#include "MTAChaosTypes.h"
#include "MTAChaosSubsystem.generated.h"

class UDataTable;
class UMTAWitnessComponent;
class UMTAAegisCameraComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTACrimeSignature, const FMTACrimeEvent&, Crime);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTACrimeWitnessedSignature, const FMTACrimeEvent&, Crime, UMTAWitnessComponent*, Witness);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTAPlateCapturedSignature, const FMTACrimeEvent&, Crime, FName, PlateId, AActor*, CameraActor);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTASubjectSightedSignature, AActor*, Subject, FVector, Location, EMTASightingSource, Source);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAAegisProfileSignature, float, NewProfile, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAReputationSignature, FName, DistrictId, float, NewReputation);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTAHeatRequestSignature, FName, DistrictId, float, Amount);

/**
 * Chaos-Systemik (Konzept Kapitel 9.7: "Chaos wird nicht bestraft, sondern erzeugt Folgen").
 *
 * Sammelt Verbrechen (FMTACrimeEvent), verteilt sie an registrierte Zeugen und Aegis-Kameras,
 * fuehrt das Kennzeichenregister, das globale Aegis-Profil und den Bezirks-Ruf und bucht
 * Fahndung ueber UMTAHeatSubsystem::AddHeat. Polizei-KI, HUD, Radio und Story binden nur an
 * die Delegates - es gibt keine Abhaengigkeit in die Gegenrichtung.
 */
UCLASS()
class MTA6_API UMTAChaosSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	// --- UWorldSubsystem / FTickableGameObject ---------------------------------------
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

	// --- Ereignisse (Polizei-KI/HUD/Radio binden hier) ---------------------------------

	/** Jedes neue Verbrechen (auch unbemerkte). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTACrimeSignature OnCrimeCommitted;

	/** Ein Zeuge hat die Tat gesehen (Meldung folgt verzoegert). NPC-KI: Flucht/Panik. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTACrimeWitnessedSignature OnCrimeWitnessed;

	/** Erste Meldung bei den Behoerden (Zeuge oder Aegis). Polizei-KI: Einsatz ausloesen. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTACrimeSignature OnCrimeReported;

	/** Eine Kollision wurde nachtraeglich zur Fahrerflucht. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTACrimeSignature OnHitAndRun;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTAPlateCapturedSignature OnPlateCaptured;

	/** "Zuletzt gesehen": Position fuer die Suchphase der Polizei. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTASubjectSightedSignature OnSubjectSighted;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTAAegisProfileSignature OnAegisProfileChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTAReputationSignature OnReputationChanged;

	/** Wird zusaetzlich zu UMTAHeatSubsystem::AddHeat gefeuert (fuer Systeme ohne Heat-Abfrage). */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|Chaos")
	FMTAHeatRequestSignature OnHeatRequested;

	// --- Verbrechen ----------------------------------------------------------------------

	/** Registriert ein Verbrechen, verteilt es an Zeugen und gibt die vergebene ID zurueck. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	int32 ReportCrime(FMTACrimeEvent Crime);

	/** Bequemer Aufruf aus C++/Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	int32 ReportSimpleCrime(EMTACrimeType Type, AActor* Perpetrator, FVector Location, AActor* Victim = nullptr, float Severity = 1.f);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	bool GetCrime(int32 CrimeId, FMTACrimeEvent& OutCrime) const;

	/** Alle Verbrechen der letzten MaxAgeSeconds (neueste zuerst). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	TArray<FMTACrimeEvent> GetRecentCrimes(float MaxAgeSeconds = 120.f) const;

	/** Regel fuer einen Typ (DataTable-Zeile oder Code-Standard). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Chaos")
	FMTACrimeRule GetRule(EMTACrimeType Type) const;

	/** Optionale Regeltabelle (Zeilenstruktur FMTACrimeRule, Zeilenname = Enum-Name). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	void SetRuleTable(UDataTable* InTable) { RuleTable = InTable; }

	static FMTACrimeRule MakeDefaultRule(EMTACrimeType Type);

	// --- Zeugen und Kameras --------------------------------------------------------------

	void RegisterWitness(UMTAWitnessComponent* Witness);
	void UnregisterWitness(UMTAWitnessComponent* Witness);
	void RegisterCamera(UMTAAegisCameraComponent* Camera);
	void UnregisterCamera(UMTAAegisCameraComponent* Camera);

	/** Wird von UMTAAegisCameraComponent nach erfolgreichem Wurf aufgerufen. */
	void HandleCameraCapture(AActor* Subject, UMTAAegisCameraComponent* Camera, float SpeedKmh, bool bFaceCaptured);

	/** Meldet eine Sichtung (Polizei-KI nutzt sie als letzte bekannte Position). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	void NotifySighting(AActor* Subject, FVector Location, EMTASightingSource Source);

	/** Ein Zeuge wurde eingeschuechtert/ausgeschaltet: seine offenen Meldungen entfallen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	void CancelPendingReports(UMTAWitnessComponent* Witness);

	// --- Spieler-Subjekte (das, wonach Kameras und Zeugen suchen) ------------------------

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	void RegisterPlayerSubject(AActor* Subject);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos")
	void UnregisterPlayerSubject(AActor* Subject);

	TArray<AActor*> GetPlayerSubjects() const;

	/** Gehoert Subject zum Taeter dieses Verbrechens (Figur, ihr Fahrzeug oder Insasse)? */
	static bool IsSubjectOfCrime(const FMTACrimeEvent& Crime, const AActor* Subject);

	// --- Fahrzeuge und Kennzeichen -------------------------------------------------------

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Vehicle")
	FMTAVehicleRecord GetOrCreateVehicleRecord(AActor* Vehicle);

	UFUNCTION(BlueprintPure, Category = "MTA6|Chaos|Vehicle")
	bool HasVehicleRecord(const AActor* Vehicle) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Vehicle")
	void SetVehiclePlayerOwned(AActor* Vehicle, bool bOwned);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Vehicle")
	void MarkVehicleStolen(AActor* Vehicle);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Vehicle")
	void SetVehicleHot(AActor* Vehicle, bool bHot);

	/** Kennzeichenwechsel: neues Kennzeichen (leer = zufaellig), nicht mehr "heiss". */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Vehicle")
	FName SwapPlate(AActor* Vehicle, FName NewPlate = NAME_None);

	/** Ueberwacht Kollisionen eines Fahrzeugs (Unfall, Sachschaden, Fahrerflucht). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Vehicle")
	void WatchVehicle(AActor* Vehicle, AActor* Driver);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Vehicle")
	void UnwatchVehicle(AActor* Vehicle);

	// --- Aegis-Profil, Ruf, Erscheinungsbild ---------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Chaos|Aegis")
	float GetAegisProfile() const { return AegisProfile; }

	/** Aendert das Aegis-Profil (0..100). Story kann den Wert ueber OnAegisProfileChanged spiegeln. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Aegis")
	void AddAegisProfile(float Delta);

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Aegis")
	void SetAegisProfile(float NewValue);

	UFUNCTION(BlueprintPure, Category = "MTA6|Chaos|Reputation")
	float GetDistrictReputation(FName DistrictId) const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Reputation")
	void AddDistrictReputation(FName DistrictId, float Delta);

	/** Kleidungswechsel: Zeugen/Kameras erkennen das alte Outfit nicht mehr sicher. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Chaos|Aegis")
	void NotifyAppearanceChanged(AActor* Subject, FName NewOutfitId);

	/** Outfit, das die Behoerden zuletzt mit dem Spieler verbinden (NAME_None = unbekannt). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Chaos|Aegis")
	FName GetKnownOutfit() const { return KnownOutfitId; }

	// --- Bezirke ---------------------------------------------------------------------------

	/** Bezirks-Resolver (vom Stadtgenerator/GameMode gesetzt). Ohne Resolver gilt DefaultDistrictId. */
	TFunction<FName(const FVector&)> DistrictResolver;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	FName DefaultDistrictId = TEXT("Core");

	UFUNCTION(BlueprintPure, Category = "MTA6|Chaos")
	FName ResolveDistrict(const FVector& Location) const;

	// --- Abstimmung --------------------------------------------------------------------

	/** Relativgeschwindigkeit (cm/s), ab der ein Treffer als Unfall zaehlt (~18 km/h). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos|Tuning")
	float MinCrashSpeed = 500.f;

	/** Entfernung (cm) vom Unfallort, ab der ohne Anhalten Fahrerflucht vorliegt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos|Tuning")
	float HitAndRunDistance = 4000.f;

	/** So lange (s) muss man nach einem Unfall in der Naehe stehen bleiben, um keine Fahrerflucht zu begehen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos|Tuning")
	float HitAndRunStopTime = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos|Tuning")
	int32 MaxStoredCrimes = 256;

	/** Anteil der Heat, den jede weitere Zeugenmeldung desselben Verbrechens noch bucht. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos|Tuning")
	float ExtraWitnessHeatFraction = 0.15f;

	/** Aegis-Profil-Zuwachs, wenn ein heisses Kennzeichen erneut gesehen wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos|Tuning")
	float HotPlateSightingAegisDelta = 0.5f;

private:
	struct FPendingReport
	{
		int32 CrimeId = INDEX_NONE;
		TWeakObjectPtr<UMTAWitnessComponent> Witness;
		float ReportTime = 0.f;
	};

	struct FHitAndRunWatch
	{
		int32 CrimeId = INDEX_NONE;
		TWeakObjectPtr<AActor> Vehicle;
		FVector Location = FVector::ZeroVector;
		float StartTime = 0.f;
		float StoppedFor = 0.f;
	};

	UFUNCTION()
	void HandleWatchedVehicleHit(AActor* SelfActor, AActor* OtherActor, FVector NormalImpulse, const FHitResult& Hit);

	/** Geteilter Zeiger: bleibt gueltig, auch wenn Delegates waehrenddessen neue Verbrechen melden. */
	TSharedPtr<FMTACrimeEvent> FindCrimeMutable(int32 CrimeId) const;
	/** Verbucht Erstwissen (Heat, Ruf, Delegate) genau einmal. */
	void MarkKnown(const TSharedPtr<FMTACrimeEvent>& Crime, EMTACrimeKnowledge NewKnowledge);
	void DistributeToWitnesses(int32 CrimeId, const FMTACrimeRule& Rule);
	void ConfirmReport(int32 CrimeId, UMTAWitnessComponent* Witness);
	/** Bucht Heat fuer ein Verbrechen (Fraction der Regel-Heat), ruft UMTAHeatSubsystem::AddHeat. */
	void ApplyHeat(FMTACrimeEvent& Crime, float Fraction);
	void ApplyReputation(const FMTACrimeEvent& Crime, const FMTACrimeRule& Rule);
	void ProcessPendingReports(float Now);
	void ProcessHitAndRun(float DeltaTime, float Now);
	FName MakeRandomPlate();
	float GetNow() const;

	UPROPERTY()
	TObjectPtr<UDataTable> RuleTable;

	TArray<TSharedRef<FMTACrimeEvent>> Crimes;
	int32 NextCrimeId = 1;

	TArray<TWeakObjectPtr<UMTAWitnessComponent>> Witnesses;
	TArray<TWeakObjectPtr<UMTAAegisCameraComponent>> Cameras;
	TArray<TWeakObjectPtr<AActor>> PlayerSubjects;

	TMap<FObjectKey, FMTAVehicleRecord> VehicleRecords;
	/** Ueberwachte Fahrzeuge -> Fahrer. */
	TMap<FObjectKey, TWeakObjectPtr<AActor>> WatchedVehicles;
	/** Treffer-Abklingzeit je Gegenstueck. */
	TMap<FObjectKey, float> HitCooldowns;

	TArray<FPendingReport> PendingReports;
	TArray<FHitAndRunWatch> HitAndRunWatches;

	TMap<FName, float> DistrictReputation;
	float AegisProfile = 50.f;
	FName KnownOutfitId = NAME_None;
	FName CurrentOutfitId = NAME_None;
	int32 PlateCounter = 0;
};
