#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "GameFramework/Actor.h"
#include "MTANPCTypes.h"
#include "MTANPCSubsystem.generated.h"

class UMTARelationshipSubsystem;
class UMTAGossipSubsystem;
class UMTADialogueSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTANPCActivityChanged, FName, NPCId, const FMTAScheduleState&, NewState);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTANPCInteractionSignature, const FMTANPCInteractionEvent&, Event);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FMTANPCSimStep, int32, Day, float, Hour);

/** Verfuegbarkeitsregeln fuer Ereignisse (aus npcs.json/scheduleRules/events). */
struct FMTAScheduleEventRule
{
	TArray<FName> Factions;
	TMap<EMTAActivity, EMTAActivity> Remap;
	FName Location;
};

/**
 * Zentrales NPC-Modell (Schluesselfiguren) mit Uhr, Tagesablaeufen und Sim-LOD.
 *
 * ABLAUF:
 *   Die Spielzeit kommt von aussen (SetGameTime vom Tageszeit-Agenten) oder per AdvanceTime.
 *   Je vergangener Spielstunde: (1) Zeitplaene neu auswerten (Wetter/Ereignisse), (2) Beziehungs-
 *   Subsystem simuliert NPC-NPC-Interaktionen je Ort, (3) Geruechte-Subsystem verbreitet Geruechte,
 *   (4) Stimmungen driften. Near/Mid-NPCs werden einzeln simuliert, Far-NPCs statistisch (Sim-LOD).
 *
 * Kein Zufall in Story-Entscheidungen: Zufall (FRandomStream mit festem Seed je Stunde) nur fuer
 * NPC-NPC-Leben; Dialog-Pruefungen sind deterministisch (UMTADialogueSubsystem).
 */
UCLASS()
class MTA6_API UMTANPCSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Laedt Content/Data/npcs.json (oder Pfad). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	bool LoadNPCs(const FString& Path = TEXT(""));

	// --- Abfragen ---------------------------------------------------------------------------

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	TArray<FName> GetAllNPCIds() const;

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	bool GetProfile(FName NPCId, FMTANPCProfile& OutProfile) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	bool GetRuntime(FName NPCId, FMTANPCRuntime& OutRuntime) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	TArray<FName> GetNPCsAtLocation(FName LocationTag) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	TArray<FName> GetNPCsInDistrict(FName District) const;

	/** Ansprechbar? (Zeitplanblock "t", nicht schlafend, nicht beschaeftigt, aktiv). */
	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	bool IsAvailableForDialogue(FName NPCId) const;

	/** Handelt gerade? (Zeitplanblock "h"). */
	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	bool IsTrading(FName NPCId) const;

	/** Bezirk eines Orts-Tags (None, wenn unbekannt). */
	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	FName GetLocationDistrict(FName LocationTag) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	FString GetLocationName(FName LocationTag) const;

	/** Wertet den Zeitplan eines NPC zu einer absoluten Spielstunde aus (rein, ohne Seiteneffekt). */
	UFUNCTION(BlueprintPure, Category = "MTA6|Schedule")
	FMTAScheduleState EvaluateSchedule(FName NPCId, float AbsoluteHour) const;

	/** Bezirksnachbarn mit Verzoegerung in Stunden (fuer Geruechte). */
	const TMap<FName, float>* GetDistrictNeighbors(FName District) const { return DistrictNeighbors.Find(District); }
	const TArray<FName>& GetDistricts() const { return DistrictIds; }

	/** Anzeigename eines Bezirks (aus npcs.json/districts). */
	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	FString GetDistrictName(FName District) const { const FString* N = DistrictNames.Find(District); return N ? *N : District.ToString(); }
	const FMTANPCProfile* FindProfile(FName NPCId) const { return Profiles.Find(NPCId); }
	const FMTANPCRuntime* FindRuntime(FName NPCId) const { return Runtime.Find(NPCId); }

	// --- Welt-Eingaenge ---------------------------------------------------------------------

	/** Setzt die Spielzeit; ist sie spaeter als die aktuelle, wird die Differenz simuliert (max. 48 h). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void SetGameTime(int32 Day, float Hour);

	/** Simuliert die angegebene Spielzeit (Stunden) weiter. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void AdvanceTime(float GameHours);

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	int32 GetDay() const { return FMath::FloorToInt32(TotalHours / 24.0); }

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	float GetHour() const { return (float)FMath::Fmod(TotalHours, 24.0); }

	double GetTotalHours() const { return TotalHours; }

	/** Wetter-ID: clear, rain, storm, fog, heat (vom Wetter-Agenten gemeldet). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void SetWeather(FName WeatherId);

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	FName GetWeather() const { return Weather; }

	/** Bezirksereignis an/aus: strike, raid, blackout, lockdown, festival (Story-/Welt-Agenten). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void SetDistrictEvent(FName District, FName EventId, bool bActive);

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	bool HasDistrictEvent(FName District, FName EventId) const;

	/** Bezirk des Spielers (bestimmt Sim-LOD Mid). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void SetPlayerDistrict(FName District);

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	FName GetPlayerDistrict() const { return PlayerDistrict; }

	/** Sichtbarer Actor fuer einen NPC (muss IMTANPCActor implementieren) -> Sim-LOD Near. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void RegisterNPCActor(FName NPCId, AActor* Actor);

	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void UnregisterNPCActor(FName NPCId);

	UFUNCTION(BlueprintPure, Category = "MTA6|NPC")
	AActor* GetNPCActor(FName NPCId) const;

	/** NPC aus der Welt nehmen/zurueckholen (Story: verhaftet, geflohen, tot). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	void SetNPCActive(FName NPCId, bool bActive);

	/** Markiert NPC als beschaeftigt (z. B. im Gespraech) fuer die angegebene Spielzeit. */
	void SetBusy(FName NPCId, float GameHours);
	void AddMood(FName NPCId, float Delta);

	/** Bequemer Einstieg fuer die Interaktions-Komponente: leitet an UMTADialogueSubsystem weiter. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	bool StartConversation(FName NPCId);

	// --- Echtzeit-Uhr (optional) ------------------------------------------------------------

	/** Wenn > 0, laeuft die NPC-Uhr selbst (Spielminuten je Echtsekunde). Standard 0: Uhr kommt von aussen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|NPC")
	float AutoGameMinutesPerSecond = 0.f;

	// --- Test und Persistenz ----------------------------------------------------------------

	/**
	 * Simuliert Tage und meldet Statistik (Interaktionen, Geruechte, Beziehungsdrift).
	 * bRestoreState = true stellt danach den vorherigen Zustand wieder her.
	 */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	FMTANPCSimStats RunNPCSimulation(int32 Days = 7, bool bRestoreState = true);

	/** Gesamtzustand (NPC, Beziehungen, Geruechte, Dialogzustand) fuer den SaveManager. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	FString SaveStateToJson() const;

	UFUNCTION(BlueprintCallable, Category = "MTA6|NPC")
	bool LoadStateFromJson(const FString& Json);

	/** Nur NPC-Laufzeitteil. */
	FString ToJson() const;
	bool FromJson(const FString& Json);

	// --- Delegates --------------------------------------------------------------------------

	UPROPERTY(BlueprintAssignable, Category = "MTA6|NPC")
	FMTANPCActivityChanged OnNPCActivityChanged;

	/** Jede sichtbare (Near/Mid) NPC-NPC-Interaktion. */
	UPROPERTY(BlueprintAssignable, Category = "MTA6|NPC")
	FMTANPCInteractionSignature OnNPCInteraction;

	UPROPERTY(BlueprintAssignable, Category = "MTA6|NPC")
	FMTANPCSimStep OnSimulationHour;

	/** Laufende Statistik (wird von RunNPCSimulation zurueckgesetzt). */
	FMTANPCSimStats& GetLiveStats() { return LiveStats; }

	/** Wird von Relationship-Subsystem aufgerufen, um Events zu verteilen (Delegate + Actor). */
	void BroadcastInteraction(const FMTANPCInteractionEvent& Event);

private:
	void StepHour();
	void RefreshSchedules(bool bBroadcast);
	void UpdateLOD();
	FName ResolveLocationToken(const FMTANPCProfile& P, FName Token) const;
	bool Tick(float DeltaSeconds);

	TMap<FName, FMTANPCProfile> Profiles;
	TArray<FName> NPCOrder;
	TMap<FName, FMTANPCRuntime> Runtime;
	TMap<FName, TArray<FMTAScheduleBlock>> Schedules;
	TMap<FName, TMap<EMTAActivity, EMTAActivity>> WeatherRules;
	TMap<FName, FMTAScheduleEventRule> EventRules;
	TMap<FName, FName> LocationDistrict;
	TMap<FName, FString> LocationNames;
	/** Ortsart je Tag (home, work, bar, food, shop, market, public). */
	TMap<FName, FName> LocationKinds;
	/** Ortsarten mit Dach (Unterstand bei Wetter). */
	TSet<FName> ShelterKinds;
	TMap<FName, TMap<FName, float>> DistrictNeighbors;
	TArray<FName> DistrictIds;
	TMap<FName, FString> DistrictNames;

	/** Aktive Ereignisse je Bezirk. */
	TMap<FName, TSet<FName>> DistrictEvents;

	UPROPERTY()
	TMap<FName, TWeakObjectPtr<AActor>> Actors;

	bool bInitialized = false;
	double TotalHours = 8.0;
	double SimulatedUntil = 8.0;
	double AutoAccumulator = 0.0;
	FName Weather = TEXT("clear");
	FName PlayerDistrict;
	FMTANPCSimStats LiveStats;
	FTSTicker::FDelegateHandle TickHandle;

	UPROPERTY()
	TObjectPtr<UMTARelationshipSubsystem> Relationships;

	UPROPERTY()
	TObjectPtr<UMTAGossipSubsystem> Gossip;
};
