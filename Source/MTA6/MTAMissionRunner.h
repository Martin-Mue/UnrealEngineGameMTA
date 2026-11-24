#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Dom/JsonObject.h"
#include "MTAMinimapTypes.h"
#include "MTAMissionTypes.h"
#include "MTAMissionRunner.generated.h"

class AMTA6GameMode;
class AMTAChaseTarget;
class AMTAMissionPickup;
class UMTAStorySubsystem;

/** Spielerzustand fuer einen Missionsschritt (echt oder im Selbsttest simuliert). */
struct FMTAMissionPlayerProbe
{
	FVector Location = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	bool bValid = false;
	bool bInVehicle = false;
	bool bWrecked = false;
	/** Fahrzeuggesundheit 0..1 (1 ohne Fahrzeug). */
	float VehicleHealth = 1.f;
	TWeakObjectPtr<AActor> Actor;
};

/** Fahrweise des simulierten Spielers im Selbsttest. */
enum class EMTAMissionSelfTestDrive : uint8
{
	/** Ziele direkt erfuellen. */
	Normal,
	/** Am Startpunkt stehen bleiben (Zeitablauf). */
	Idle,
	/** Weit weg vom Ziel bleiben (Zielverlust). */
	StayFar,
	/** Flucht durch Abkuehlen statt Versteck. */
	LoseByCooling
};

/**
 * Missionssystem (docs/MISSIONSTYPEN.md): macht aus den Story-Missionen (Feld "type"/"params"
 * in Content/Data/story.json) spielbare Fahrteile mit Schritten, Zeitlimit und Fehlschlag.
 * Haengt am AMTA6GameMode (siehe docs/INTEGRATION_MISSIONEN.md) oder wird ueber Get() erzeugt.
 *
 * Ablauf:  Idle --Select(Id)--> Briefing --(erster Tick ohne Dialog / ConfirmBriefing)--> Running
 *          Running --alle Schritte--> Success | --Zeit/Zerstoerung/Zielverlust/Festnahme/Tod--> Failed
 *          Success/Failed --ResultDisplayTime--> Idle.  Abort() jederzeit -> Idle.
 *
 * Story: Bei Erfolg feuert OnMissionFinished(true). Der Dialog mit Text/choices und
 * UMTAStorySubsystem::CompleteMission bleibt Aufgabe der UMTAMissionComponent (Fahrteil zuerst,
 * Entscheidung danach). Mit bCompleteStoryOnSuccess = true schliesst der Runner selbst ab.
 * Der Runner zahlt nur Zuschlaege (Bezirksfaktor, Zeitbonus, Platz, Einsatz) bzw. 25 % Kosten
 * bei Fehlschlag; "money" aus onSuccess zahlt die Story-Engine.
 *
 * Orte: params nennen Orte nur als Namen ("saltmarket_pension"). Aufloesung deterministisch:
 *   1. registry.locations[Name] = [x, y] (optional in story.json)
 *   2. Bezirkspraefix im Namen -> eine Kreuzung im Bezirk (Bezirkszellen aus den Missionszellen)
 *   3. "variable"/"chosen_ally" -> weit entfernte Kreuzung; "any"/leer -> Schritt entfaellt
 *   4. sonst Kreuzung 2..4 Bloecke vom Auftraggeber (Hash des Namens)
 */
UCLASS(ClassGroup = (MTA6), meta = (BlueprintSpawnableComponent))
class MTA6_API UMTAMissionRunner : public UActorComponent, public IMTAMinimapProvider
{
	GENERATED_BODY()

public:
	UMTAMissionRunner();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Runner am GameMode finden oder (falls nicht vorhanden) dort anlegen. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission", meta = (WorldContext = "WorldContextObject"))
	static UMTAMissionRunner* Get(const UObject* WorldContextObject);

	// --- Einstellungen -------------------------------------------------------------------

	/** Story-Mission bei Erfolg selbst abschliessen (sonst: UMTAMissionComponent zeigt den Dialog). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	bool bCompleteStoryOnSuccess = false;

	/** Freie Kurierfahrt als letzten Listeneintrag anbieten. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	bool bOfferFreeCourier = true;

	/** So lange (s) bleibt Erfolg/Fehlschlag sichtbar, danach Idle. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float ResultDisplayTime = 5.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float ArrivalRadius = 900.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float CheckpointRadius = 1100.f;

	/** Abholen/Abliefern: langsamer als dies (cm/s) gilt als "angehalten". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float StopSpeed = 700.f;

	/** Hack/Event: Radius des Haltebereichs (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float HoldZoneRadius = 2500.f;

	/** Verfolgung: Ziel gilt als entkommen, wenn laenger als ChaseLoseTime weiter weg als ChaseLoseDistance. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float ChaseLoseDistance = 25000.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float ChaseLoseTime = 10.f;

	/** Blockieren: so lange (s) muss das Ziel stehen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float BlockCaptureTime = 3.f;

	/** Beschatten: so lange (s) zu nah = entdeckt. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float ObserveDetectTime = 3.f;

	/** Ohne Spieler-Pawn laenger als dies (s) = Tod. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float NoPawnFailTime = 3.f;

	/** Fahrgast an Bord und Spieler laenger als dies (s) ausserhalb des Fahrzeugs = Fahrgast weg. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float PassengerLeaveTime = 4.f;

	/** Anteil des Basisgelds, der bei Fehlschlag als Kosten abgezogen wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float FailCostFraction = 0.25f;

	/** Angenommene Durchschnittsgeschwindigkeit fuer berechnete Zeitlimits (cm/s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Mission")
	float ReferenceSpeed = 1400.f;

	// --- Ereignisse ----------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Mission")
	FMTAMissionObjectiveSignature OnObjectiveChanged;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Mission")
	FMTAMissionFinishedSignature OnMissionFinished;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|Mission")
	FMTAMissionRunStateSignature OnRunStateChanged;

	// --- Auftragsauswahl -----------------------------------------------------------------

	/** Verfuegbare Auftraege (Haupt vor Neben, hoechstens 9 fuer die Zifferntasten). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	TArray<FMTAMissionOffer> GetAvailable();

	/** Auftragsliste oeffnen/schliessen (HUD zeigt sie, Ziffern waehlen, Esc schliesst). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void OpenSelection();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void CloseSelection();

	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void ToggleSelection();

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool IsSelectionOpen() const { return bSelectionOpen; }

	/** Beim Oeffnen zwischengespeicherte Liste (fuer das HUD, keine erneute Story-Abfrage pro Frame). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	TArray<FMTAMissionOffer> GetSelectionOffers() const { return SelectionOffers; }

	/** Ziffer 1..9 in der Liste (Index 0..8). Ziffer 0 bricht einen laufenden Auftrag ab. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	bool SelectByIndex(int32 Index);

	/** Mission waehlen. bStartImmediately: Start beim ersten Tick ohne offenen Dialog (Briefing-Seite). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	bool Select(const FString& MissionId, bool bStartImmediately = true);

	/** Briefing bestaetigen (nur noetig mit bStartImmediately = false). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void ConfirmBriefing();

	/** Laufenden Auftrag abbrechen (keine Kosten, OnMissionFinished(false)). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void Abort();

	/** Zuletzt gescheiterten Auftrag neu starten (Checkpoint-Neustart = von vorn). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	bool Retry();

	/** Eine bereits gebaute Mission starten (Selbsttest, Skripte). */
	bool StartSpec(const FMTAMissionSpec& InSpec, bool bStartImmediately = true);

	// --- Zustand / Anzeige ----------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	EMTAMissionRunState GetState() const { return State; }

	/** Briefing oder Running. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool HasActiveMission() const { return State == EMTAMissionRunState::Briefing || State == EMTAMissionRunState::Running; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FMTAMissionSpec GetActiveMission() const { return Spec; }

	/** C++-Zugriff ohne Kopie. */
	const FMTAMissionSpec& GetActiveMissionRef() const { return Spec; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	int32 GetStepIndex() const { return StepIndex; }

	/** Zieltext ("Liefere das Paket an: Core Tower Plaza"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetObjectiveText() const;

	/** Zusatzzeile mit Live-Werten ("Abstand 85 m", "Upload 40 %", "Platz 2/4"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetStatusText() const;

	/** Kopfzeile fuer das HUD ("Verfolgung: Streikbrecher"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetTitleText() const;

	/** Restzeit in s, < 0 = kein Zeitlimit (oder noch nicht gestartet). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	float GetTimeLeft() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	int32 GetProgressCurrent() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	int32 GetProgressTotal() const;

	/** "2/4 Checkpoints" oder leer. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetProgressText() const;

	/** Alle Kartenpunkte (aktuelles Ziel zuerst). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	TArray<FMTAMissionMarker> GetObjectiveMarkers() const;

	/** Nur die Positionen (fuer einfache Marker). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	TArray<FVector> GetObjectiveLocations() const;

	/** Hauptziel (Wegpunkt); false ohne aktives Ziel. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool GetPrimaryObjectiveLocation(FVector& OutLocation) const;

	/** Horizontaler Abstand zum Hauptziel (cm), -1 ohne. */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	float GetDistanceToObjective() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	EMTAMissionFailReason GetFailReason() const { return FailReason; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetFailReasonText() const;

	/** Vom Runner gebuchte Geldaenderung des letzten Abschlusses (Zuschlag oder Kosten). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	int32 GetLastResultMoney() const { return LastResultMoney; }

	/** Id des zuletzt beendeten Auftrags (fuer den Story-Dialog nach Erfolg). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	FString GetLastFinishedMissionId() const { return LastFinishedId; }

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	bool WasLastFinishedFromStory() const { return bLastFinishedFromStory; }

	/** Anzeigenamen (ASCII). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	static FString GetTypeDisplayName(EMTAMissionType InType);

	UFUNCTION(BlueprintPure, Category = "MTA6|Mission")
	static FString GetDistrictDisplayName(const FString& DistrictId);

	static EMTAMissionType ParseType(const FString& TypeName);

	// --- Missionsbau ----------------------------------------------------------------------

	/** Baut eine Story-Mission aus story.json (ungueltig, wenn die Id fehlt). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	FMTAMissionSpec BuildStorySpec(const FString& MissionId);

	/** Baut eine Mission aus Typ und params-JSON (Selbsttest, Skripte). Money = Basisgeld. */
	FMTAMissionSpec BuildSpec(const FString& Id, const FString& Title, const FString& TypeName, const FString& ParamsJson,
		FIntPoint Cell, const FString& District, int32 Money, bool bMain);

	/** Freie Kurierfahrt vom Spieler zu einer entfernten Kreuzung. */
	FMTAMissionSpec BuildFreeCourierSpec();

	// --- Externe Meldungen --------------------------------------------------------------

	/** Festnahme (vom GameMode OnBusted, wird in BeginPlay automatisch gebunden). */
	UFUNCTION()
	void HandleBusted();

	/** Tod des Spielers melden (sobald es ein Todessystem gibt). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	void NotifyPlayerDied();

	// --- Simulation -----------------------------------------------------------------------

	/** Ein Missionsschritt (TickComponent ruft das auf, der Selbsttest direkt). */
	void StepMission(float DeltaSeconds);

	/** Testhilfe: spielt jeden Missionstyp ohne Eingabe durch und liefert einen Bericht. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|Mission")
	FString RunMissionSelfTest();

	// --- IMTAMinimapProvider --------------------------------------------------------------
	virtual void GatherMinimapBlips(TArray<FMTAMinimapBlip>& OutBlips) const override;

private:
	FMTAMissionPlayerProbe GetPlayerProbe() const;
	AMTA6GameMode* GetMTAGameMode() const;
	UMTAStorySubsystem* GetStory() const;

	bool EnsureStoryData();
	TSharedPtr<FJsonObject> FindStoryMission(const FString& MissionId) const;
	FMTAMissionSpec BuildSpecFromJson(const TSharedPtr<FJsonObject>& Mission);
	void BuildSteps(FMTAMissionSpec& Out, const TSharedPtr<FJsonObject>& Params);

	/** Ortsnamen aufloesen (siehe Klassenkommentar). false = Ort entfaellt ("any"). */
	bool ResolvePlace(const FString& Name, const FIntPoint& Anchor, const FString& District, int32 Salt,
		const TArray<FIntPoint>& Avoid, FIntPoint& OutCell, FString& OutDisplay) const;
	FVector CellToGround(const FIntPoint& Cell) const;
	float FindGroundZ(float X, float Y) const;
	FString DistrictFromName(const FString& Name) const;

	void ChangeState(EMTAMissionRunState NewState);
	void BeginRun();
	void EnterStep(int32 Index);
	void AdvanceStep(const FMTAMissionPlayerProbe& Probe);
	void Succeed();
	void Fail(EMTAMissionFailReason Reason);
	void Cleanup();
	void SpawnMarkers();
	void RefreshMarkers();
	void StepCurrent(float Dt, const FMTAMissionPlayerProbe& Probe);
	void StepChase(float Dt, const FMTAMissionPlayerProbe& Probe, const FMTAMissionStep& Step);
	void StepRace(float Dt, const FMTAMissionPlayerProbe& Probe);
	float GetPlayerRaceProgress(const FMTAMissionPlayerProbe& Probe) const;
	int32 ComputeRacePlace(const FMTAMissionPlayerProbe& Probe) const;

	float GetHeatValue() const;
	void SetHeatValue(float NewHeat);
	void ReportHeat(float Amount);
	void AddMoneyValue(int32 Amount);
	void Toast(const FString& Text) const;

	// --- Selbsttest -----------------------------------------------------------------------
	EMTAMissionRunState DriveSelfTest(float MaxSeconds, EMTAMissionSelfTestDrive Drive, int32& OutSteps, float& OutSeconds);
	void SetSimPlayer(const FVector& Location, const FVector& Velocity);

	// --- Daten ----------------------------------------------------------------------------
	TSharedPtr<FJsonObject> StoryData;
	TMap<FString, TArray<FIntPoint>> DistrictCells;

	EMTAMissionRunState State = EMTAMissionRunState::Idle;
	FMTAMissionSpec Spec;
	FMTAMissionSpec LastFailedSpec;
	int32 StepIndex = 0;
	bool bAutoStart = true;

	float TimeLeft = 0.f;
	bool bTimerRunning = false;
	float Elapsed = 0.f;
	float StepTimer = 0.f;
	float HoldProgress = 0.f;
	bool bDirectAccess = false;
	bool bCargoLoaded = false;
	bool bPassengerInVehicle = false;
	float CargoHealthBaseline = 1.f;
	float OutOfVehicleTimer = 0.f;
	float NoPawnTimer = 0.f;
	float ChaseLostTimer = 0.f;
	float ChaseCloseTimer = 0.f;
	float ChaseObserveTimer = 0.f;
	float StepEnterTime = 0.f;
	bool bBustedPending = false;
	bool bDiedPending = false;
	float ResultTimer = 0.f;
	int32 RacePlace = 0;
	float LastHealthSeen = 1.f;

	EMTAMissionFailReason FailReason = EMTAMissionFailReason::None;
	int32 LastResultMoney = 0;
	FString LastFinishedId;
	bool bLastFinishedFromStory = false;

	bool bSelectionOpen = false;
	TArray<FMTAMissionOffer> SelectionOffers;

	// Simulierter Spieler (Selbsttest)
	bool bSimulatePlayer = false;
	bool bSelfTestActive = false;
	FMTAMissionPlayerProbe SimProbe;
	float LocalHeat = 0.f;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTAMissionPickup>> StepMarkers;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTAMissionPickup>> ZoneMarkers;

	UPROPERTY(Transient)
	TObjectPtr<AMTAChaseTarget> ChaseTarget;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTAChaseTarget>> EscortTargets;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTAChaseTarget>> Opponents;

	/** Rennstrecke (Startlinie + alle Checkpoints) und kumulierte Laengen. */
	TArray<FVector> RacePath;
	TArray<float> RaceCumulative;
	bool bBustedBound = false;
};
