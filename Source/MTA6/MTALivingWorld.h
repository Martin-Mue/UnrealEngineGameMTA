#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "MTANPCTypes.h"
#include "MTAStreetCrowd.h"
#include "MTALivingWorld.generated.h"

class AMTALivingNPC;
class AMTACityGenerator;
class UMTANPCSubsystem;
class UMTADialogueSubsystem;
class UMTARelationshipSubsystem;
class UMTAGossipSubsystem;

/** Eine Figur der lebendigen Welt hat etwas gesagt (Bark, Gespraechszeile, Gruss). Fuer HUD-Untertitel. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FMTALivingSpeechSignature, FName, SpeakerId, const FString&, SpeakerName, const FString&, Text);

/** Ein Orts-Tag aus npcs.json, aufgeloest auf das Stadtraster. */
struct FMTALivingLocation
{
	FName Tag;
	FName District;
	/** Ortsart aus npcs.json: home, work, bar, food, shop, market, public. */
	FName Kind;
	FString Name;
	int32 BlockX = 0;
	int32 BlockY = 0;
	/** 0 = Sued (+X innen), 1 = Nord (-X innen), 2 = West (+Y innen), 3 = Ost (-Y innen). */
	int32 Side = 0;
	float Along = 0.5f;
	FMTALivingSpot Spot;
};

/** Wegpunkt entlang der Gehwege; bCurb = Bordstein vor einer Strassenquerung (kurz warten). */
struct FMTALivingWaypoint
{
	FVector Pos = FVector::ZeroVector;
	bool bCurb = false;
};

/** Zustand eines Agenten. */
enum class EMTALivingPhase : uint8
{
	/** Steht/sitzt am Ort der Aktivitaet. */
	Idle,
	/** Laeuft entlang eines Weges. */
	Walking,
	/** Drinnen (Schlafen, Zuhause, Buero): kein Actor. */
	Indoors,
	/** Gespraech/Streit mit einer anderen Figur. */
	Encounter,
	/** Gespraech mit dem Spieler. */
	TalkingToPlayer
};

/** Gewuenschter Zustand laut Tagesablauf (Ergebnis der Auswertung). */
struct FMTALivingDesired
{
	EMTAActivity Activity = EMTAActivity::Home;
	FName Tag;
	FMTALivingSpot Spot;
	FName District;
	FString LocationName;
	FName LocationKind;
	bool bIndoors = false;
	bool bTalkable = false;
	bool bTrading = false;
	bool bWander = false;
	bool bOnBreak = false;
	FName OverrideReason;
};

/** Datensatz einer Figur (Schluessel-, Neben- oder Strassenfigur). Der Actor ist nur Darstellung. */
struct FMTALivingAgent
{
	FName Id;
	FString DisplayName;
	FString Occupation;
	FName HomeDistrict;
	FName Faction;
	FName NPCRole;
	bool bKey = false;
	bool bHasDialogue = false;
	/** Index in StreetProfiles (nur Strassenfiguren). */
	int32 StreetIndex = INDEX_NONE;
	float Talk = 0.5f;
	float Temper = 0.3f;
	float WalkSpeed = 150.f;
	float Scale = 1.f;

	// Tagesablauf
	EMTAActivity Activity = EMTAActivity::Home;
	FName LocationTag;
	FName CurrentDistrict;
	FString LocationName;
	FName LocationKind;
	bool bTalkable = false;
	bool bTrading = false;
	bool bWander = false;
	bool bOnBreak = false;
	bool bIndoorAtTarget = false;
	bool bActiveInWorld = true;
	FMTALivingSpot BaseSpot;
	FVector TargetPos = FVector::ZeroVector;
	float TargetYaw = 0.f;
	int32 Slot = INDEX_NONE;
	FName SlotKey;
	FString ActivityText;
	EMTALivingPhase Phase = EMTALivingPhase::Indoors;

	// Bewegung
	TArray<FMTALivingWaypoint> Path;
	int32 PathIndex = 0;
	bool bWanderWalk = false;
	FVector SimPos = FVector::ZeroVector;
	float SimYaw = 0.f;
	float WaitTime = 0.f;
	float StuckTime = 0.f;
	float SidestepTime = 0.f;
	float SidestepSign = 1.f;
	double NextWanderTime = 0.0;
	double NextLookTime = 0.0;
	float LookYawOffset = 0.f;

	// Soziales
	int32 EncounterId = INDEX_NONE;
	double NextSocialTime = 0.0;
	double NextBarkTime = 0.0;
	double FaceUntil = 0.0;
	FVector FacePoint = FVector::ZeroVector;
	double PinnedUntil = 0.0;
	FString LastSpeech;
	double LastSpeechTime = -100.0;
	/** Statuszeile waehrend einer Begegnung ("plaudert mit ..."). */
	FString EncounterText;

	// Auswertung
	/** Zeitplan beim naechsten langsamen Schritt sofort neu auswerten. */
	bool bForceEval = false;

	TWeakObjectPtr<AMTALivingNPC> Actor;
};

/** Eine laufende Begegnung zweier Figuren (Plaudern, Streit, Handel ...). */
struct FMTALivingEncounter
{
	int32 Id = 0;
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	EMTAInteractionType Type = EMTAInteractionType::Chat;
	TArray<FString> Lines;
	/** 0 = A spricht, 1 = B spricht. */
	TArray<uint8> Speakers;
	int32 LineIndex = -1;
	float LineTimer = 0.f;
	float ApproachTime = 0.f;
	bool bTalking = false;
	/** Beschatten: nur A ist beteiligt, B merkt nichts. */
	bool bOneSided = false;
	float OneSidedTime = 0.f;
};

/**
 * Lebendige Welt: Schluesselfiguren und Strassenfiguren mit Tagesablauf, Wegen, Gespraechen und Barks.
 *
 * - ORTE: Die 52 Orts-Tags aus npcs.json werden deterministisch auf Gehwegpunkte im Stadtraster
 *   abgebildet (Bezirk -> Block des Bezirks, Art -> Blockseite, Hash -> Position), ResolveLocation(Tag).
 * - TAGESABLAUF: Schluesselfiguren folgen UMTANPCSubsystem::EvaluateSchedule (inkl. Wetter/Ereignisse),
 *   Strassenfiguren einfachen Zufallsplaenen (MTAStreetCrowd). Schlafen/Zuhause/Buero = drinnen (kein Actor).
 * - BEWEGUNG: ohne NavMesh auf Laufspuren der Gehwege (Abstand WalkLineOffset zur Strassenmitte), Strassen
 *   werden an Kreuzungen gequert (kurzes Warten am Bordstein). Sichtbare Figuren laufen mit AddMovementInput,
 *   unsichtbare folgen dem Weg rechnerisch schneller (Sim-LOD).
 * - BUDGET: hoechstens MaxVisibleNPCs Actors (Pool), nur innerhalb SpawnRadius um den Spieler.
 * - SOZIAL: nahe Figuren bleiben stehen, drehen sich zueinander und wechseln Sprechblasen; Schluesselfiguren
 *   ueber UMTARelationshipSubsystem::ChooseInteraction/ApplyInteraction, Geruechte ueber UMTAGossipSubsystem.
 * - BARKS: Zurufe beim Vorbeigehen/-fahren (UMTADialogueSubsystem, Kontext Wetter/Zeit/Tempo/Fahndung).
 */
UCLASS()
class MTA6_API AMTALivingWorldManager : public AActor
{
	GENERATED_BODY()

public:
	AMTALivingWorldManager();

	virtual void Tick(float DeltaSeconds) override;

	/** Erster Manager in der Welt (oder nullptr). */
	static AMTALivingWorldManager* Get(const UObject* WorldContextObject);

	/** Manager holen oder erzeugen (Einstieg fuer AMTA6GameMode::SpawnDialogueNPCs). */
	static AMTALivingWorldManager* GetOrSpawn(const UObject* WorldContextObject);

	// --- Einstellungen ----------------------------------------------------------------------

	/** Zusaetzliche generische Strassenfiguren (neben den 65 Profilen aus npcs.json). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld", meta = (ClampMin = "0", ClampMax = "200"))
	int32 NumStreetNPCs = 48;

	/** Budget sichtbarer Figuren (Actors). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld", meta = (ClampMin = "0", ClampMax = "80"))
	int32 MaxVisibleNPCs = 80;

	/** Figuren naeher als dies (cm) bekommen einen Actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld")
	float SpawnRadius = 12000.f;

	/** Actors weiter als dies (cm) gehen zurueck in den Pool (Hysterese). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld")
	float DespawnRadius = 13500.f;

	/** Neue Actors je LOD-Durchlauf (verteilt Spawn-Kosten; Pool-Actors zaehlen nicht). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld", meta = (ClampMin = "1"))
	int32 MaxSpawnsPerUpdate = 6;

	/** Namensschild/Aktivitaet sichtbar bis (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld")
	float LabelDistance = 2200.f;

	/** Sprechblasen sichtbar bis (cm). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld")
	float SpeechDistance = 3500.f;

	/** Abstand der Laufspur zur Strassenmitte (cm); zwischen Laternen (900) und Hauswand (1300). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld")
	float WalkLineOffset = 1150.f;

	/** Reisegeschwindigkeit unsichtbarer Figuren (cm/s, Sim-LOD). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld")
	float HiddenTravelSpeed = 1800.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Barks")
	bool bEnableBarks = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Barks")
	float BarkRadiusOnFoot = 650.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Barks")
	float BarkRadiusInVehicle = 1500.f;

	/** Abklingzeit je Figur (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Barks")
	float BarkCooldownPerNPC = 25.f;

	/** Mindestabstand zweier Barks insgesamt (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Barks")
	float GlobalBarkInterval = 4.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Social")
	bool bEnableSocial = true;

	/** Abstand (cm), ab dem zwei stehende Figuren ins Gespraech kommen koennen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Social")
	float SocialRadius = 350.f;

	/** Pruefintervall fuer neue Begegnungen (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Social")
	float SocialCheckInterval = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld|Social")
	int32 MaxConcurrentEncounters = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|LivingWorld")
	int32 Seed = 1337;

	// --- Abfragen ---------------------------------------------------------------------------

	/** Weltposition (Gehweg) eines Orts-Tags aus npcs.json; ZeroVector, wenn unbekannt. */
	UFUNCTION(BlueprintPure, Category = "MTA6|LivingWorld")
	FVector ResolveLocation(FName Tag) const;

	/** Wie ResolveLocation, mit Ausrichtung. */
	bool TryResolveLocation(FName Tag, FMTALivingSpot& OutSpot) const;

	UFUNCTION(BlueprintPure, Category = "MTA6|LivingWorld")
	int32 GetAgentCount() const { return Agents.Num(); }

	UFUNCTION(BlueprintPure, Category = "MTA6|LivingWorld")
	int32 GetStreetNPCCount() const { return StreetProfiles.Num(); }

	UFUNCTION(BlueprintPure, Category = "MTA6|LivingWorld")
	int32 GetVisibleNPCCount() const { return VisibleIndices.Num(); }

	/** Naechste sichtbare Figur innerhalb MaxDistance (fuer HUD-Rueckfall "[E] ..."). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|LivingWorld")
	AMTALivingNPC* FindNearestTalkableNPC(const FVector& Location, float MaxDistance = 500.f) const;

	/** Juengste Sprechblase in der Naehe (fuer HUD-Untertitel). */
	UFUNCTION(BlueprintCallable, Category = "MTA6|LivingWorld")
	bool GetNearestSpeech(const FVector& Location, float MaxDistance, FString& OutSpeaker, FString& OutText) const;

	/** Holt eine ansprechbare Dialogfigur sofort neben Location (Tests, Missionen). Bleibt 60 s dort. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|LivingWorld")
	AMTALivingNPC* BringDialogueNPCTo(const FVector& Location);

	/** Prueft ohne Eingabe: Orte, Positionen je Tageszeit, Bewegung, NPC-NPC-Interaktion, Budget. Bericht als Text. */
	UFUNCTION(BlueprintCallable, Category = "MTA6|LivingWorld")
	FString RunLivingWorldSelfTest();

	UPROPERTY(BlueprintAssignable, Category = "MTA6|LivingWorld")
	FMTALivingSpeechSignature OnLivingSpeech;

	const FMTALivingAgent* GetAgent(int32 Index) const { return Agents.IsValidIndex(Index) ? &Agents[Index] : nullptr; }

	// --- Rueckmeldungen der Actors ----------------------------------------------------------

	void NotifyActivityChanged(int32 AgentIndex);
	void NotifyInteractionEvent(int32 AgentIndex, const FMTANPCInteractionEvent& Event);
	void NotifyPlayerInteract(int32 AgentIndex, AActor* Interactor);
	void NotifyConversationState(int32 AgentIndex, bool bInConversation);
	void NotifyBarkShown(int32 AgentIndex, const FString& Text, FName Category);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// Aufbau
	bool InitializeWorld();
	void BuildGrid();
	void BuildLocations();
	void BuildAgents();
	FMTALivingSpot MakeSpot(int32 BX, int32 BY, int32 Side, float Along) const;
	FMTALivingSpot RandomSidewalkSpot(FName District, uint32 Salt) const;
	FMTALivingSpot FallbackSpotForDistrict(FName District, uint32 Salt) const;
	float GroundZAt(const FVector& P) const;
	bool IsOnRoadLocal(const FVector& P) const;
	bool IsInsideGrid(const FVector& P) const;
	double GetNow() const;

	// Tagesablauf
	float GetCurrentHour() const;
	bool ComputeDesired(const FMTALivingAgent& A, float Hour, FMTALivingDesired& Out) const;
	bool DesiredDiffers(const FMTALivingAgent& A, const FMTALivingDesired& D) const;
	void ApplyDesired(int32 Index, const FMTALivingDesired& D, bool bTeleport);
	void EvaluateAgent(int32 Index, float Hour, bool bTeleport);
	FString MakeActivityText(const FMTALivingAgent& A, const FMTALivingDesired& D, float Hour) const;
	int32 AcquireSlot(FName Key, int32 Index);
	void ReleaseSlot(int32 Index);
	void ComputeSlotPose(FMTALivingAgent& A) const;

	// Wege
	void BuildPath(const FVector& From, const FVector& To, TArray<FMTALivingWaypoint>& OutPath) const;
	FVector SnapToWalkNetwork(const FVector& P, bool& bOutOnXLine) const;
	void AppendSegment(TArray<FMTALivingWaypoint>& OutPath, const FVector& From, const FVector& To) const;
	void RouteTo(int32 Index, const FVector& Dest);
	void OnArrived(int32 Index);
	FVector GetAgentLocation(const FMTALivingAgent& A) const;

	// Bewegung und Darstellung
	void SteerVisible(int32 Index, float DeltaSeconds);
	void AdvanceHidden(int32 Index, float DeltaSeconds, bool bIgnoreActors);
	void UpdateIdleBehaviour(int32 Index, double Now);
	void UpdateLOD();
	void AssignActor(int32 Index);
	void ReleaseActor(int32 Index);
	void PushTextToActor(int32 Index);

	// Soziales
	void UpdateSocial(double Now);
	bool IsSocialEligible(const FMTALivingAgent& A, double Now) const;
	bool TryStartEncounter(int32 IA, int32 IB, bool bForce, const FMTANPCInteractionEvent* SimEvent);
	void BuildEncounterLines(FMTALivingEncounter& Enc, const FMTANPCInteractionEvent& Ev);
	void UpdateEncounters(float DeltaSeconds);
	void EndEncounter(int32 EncounterIndex);
	FMTALivingEncounter* FindEncounter(int32 EncounterId);
	void Speak(int32 Index, const FString& Text, float Duration, FColor Color, bool bBroadcast);
	FString GetBarkLine(FName Category, FName District, FName Faction, FName InRole) const;
	static uint64 MakePairKey(int32 IA, int32 IB);

	// Barks und Spieler
	void UpdateBarks(double Now);
	void ChooseBarkCategories(const FMTALivingAgent& A, bool bInVehicle, float SpeedKmh, float Dist, int32 Wanted, bool bOnSidewalk, TArray<FName>& OutCategories);
	void UpdatePlayerConversation();
	int32 GetPlayerWantedLevel() const;

	// Selbsttest-Hilfen
	void ResetAllActorsAfterTest();

	// --- Daten ------------------------------------------------------------------------------

	UPROPERTY(Transient)
	TObjectPtr<UMTANPCSubsystem> NPCSys;

	UPROPERTY(Transient)
	TObjectPtr<UMTADialogueSubsystem> DialogueSys;

	UPROPERTY(Transient)
	TObjectPtr<UMTARelationshipSubsystem> RelationSys;

	UPROPERTY(Transient)
	TObjectPtr<UMTAGossipSubsystem> GossipSys;

	UPROPERTY(Transient)
	TObjectPtr<AMTACityGenerator> CityGen;

	/** Alle erzeugten Actors (gebunden oder im Pool), haelt sie fuer den GC. */
	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTALivingNPC>> SpawnedActors;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AMTALivingNPC>> ActorPool;

	TArray<FMTALivingAgent> Agents;
	TMap<FName, int32> AgentById;
	TArray<FMTAStreetProfile> StreetProfiles;
	TArray<int32> VisibleIndices;

	TMap<FName, FMTALivingLocation> Locations;
	TArray<FName> LocationOrder;
	/** Erster ueberdachter Treffpunkt (Bar/Imbiss) je Bezirk: Unterstand bei Regen fuer Strassenfiguren. */
	TMap<FName, FName> ShelterTags;
	TMap<FName, TArray<FIntPoint>> DistrictBlocks;
	TMap<FName, TArray<int32>> SlotOwners;

	TArray<FMTALivingEncounter> Encounters;
	TMap<uint64, double> PairCooldowns;
	int32 NextEncounterId = 1;

	// Raster
	TArray<float> RoadXs;
	TArray<float> RoadYs;
	TArray<float> WalkLinesX;
	TArray<float> WalkLinesY;
	int32 GridBlocksX = 8;
	int32 GridBlocksY = 8;
	float RoadHalfWidth = 800.f;
	float SidewalkZ = 15.f;
	float RoadZ = 0.f;
	/** Wirksamer Laufspur-Abstand (WalkLineOffset, geklemmt auf den Gehweg). */
	float WalkOffset = 1150.f;
	/** Selbsttest: feste Stunde statt der NPC-Uhr (< 0 = aus). */
	float TestHourOverride = -1.f;

	FRandomStream SimRng;
	FRandomStream SocialRng;

	bool bWorldReady = false;
	bool bSelfTestRunning = false;
	float InitRetryTime = 0.f;
	float SlowAccumulator = 0.f;
	float LODAccumulator = 0.f;
	float SocialAccumulator = 0.f;
	float BarkAccumulator = 0.f;
	double NextGlobalBarkTime = 0.0;
	int32 EvalCursor = 0;
	uint32 LastSimEventHash = 0;
	double LastSimEventTime = -100.0;
};
