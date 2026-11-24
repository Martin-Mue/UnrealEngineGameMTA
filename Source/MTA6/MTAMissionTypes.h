#pragma once

#include "CoreMinimal.h"
#include "MTAMissionTypes.generated.h"

/**
 * Datentypen des Missionssystems (UMTAMissionRunner, AMTAChaseTarget, AMTAMissionPickup).
 * Grundlage: docs/MISSIONSTYPEN.md und die Felder "type"/"params" in Content/Data/story.json.
 * Einheiten: cm, cm/s, Sekunden. Alle Texte fuer das HUD sind reines ASCII (ae/oe/ue statt Umlaut).
 */

/** Missionstyp laut story.json (Feld "type"). */
UENUM(BlueprintType)
enum class EMTAMissionType : uint8
{
	Courier  UMETA(DisplayName = "Kurier"),
	Chase    UMETA(DisplayName = "Verfolgung"),
	Escape   UMETA(DisplayName = "Flucht"),
	Race     UMETA(DisplayName = "Rennen"),
	Hunt     UMETA(DisplayName = "Auftragsjagd"),
	Hack     UMETA(DisplayName = "Hack"),
	Gig      UMETA(DisplayName = "Gig"),
	Side     UMETA(DisplayName = "Nebenauftrag"),
	Event    UMETA(DisplayName = "Ereignis"),
	Unknown  UMETA(DisplayName = "Unbekannt")
};

/** Zustand des Runners. */
UENUM(BlueprintType)
enum class EMTAMissionRunState : uint8
{
	Idle      UMETA(DisplayName = "Kein Auftrag"),
	Briefing  UMETA(DisplayName = "Briefing"),
	Running   UMETA(DisplayName = "Laeuft"),
	Success   UMETA(DisplayName = "Erfolg"),
	Failed    UMETA(DisplayName = "Fehlschlag")
};

/** Art eines Missionsschritts. */
UENUM(BlueprintType)
enum class EMTAMissionStepKind : uint8
{
	/** Ort erreichen (Radius). */
	Reach       UMETA(DisplayName = "Ort erreichen"),
	/** Abholen: Ort erreichen und kurz anhalten (oder zu Fuss interagieren). */
	Pickup      UMETA(DisplayName = "Abholen"),
	/** Abliefern: Ort erreichen und kurz anhalten (oder zu Fuss interagieren). */
	Deliver     UMETA(DisplayName = "Abliefern"),
	/** Durchfahren ohne Anhalten (Rennen, Kontrollpunkt, Aufkleber). */
	Checkpoint  UMETA(DisplayName = "Checkpoint"),
	/** Im Radius bleiben, bis Duration erreicht ist (Hack-Upload, Event, Verteidigung). */
	HoldZone    UMETA(DisplayName = "Bereich halten"),
	/** Zielfahrzeug stellen, rammen oder beobachten (AMTAChaseTarget). */
	Chase       UMETA(DisplayName = "Verfolgung"),
	/** Fahndung abschuetteln (Heat 0) oder ein Versteck erreichen. */
	LoseHeat    UMETA(DisplayName = "Fahndung abschuetteln")
};

/** Wie ein Zielfahrzeug gestellt wird (params.capture_mode). */
UENUM(BlueprintType)
enum class EMTAChaseCaptureMode : uint8
{
	Ram         UMETA(DisplayName = "Rammen"),
	Block       UMETA(DisplayName = "Blockieren"),
	RamOrBlock  UMETA(DisplayName = "Rammen oder Blockieren"),
	Stop        UMETA(DisplayName = "Anhalten"),
	Observe     UMETA(DisplayName = "Beschatten")
};

/** Grund eines Fehlschlags. */
UENUM(BlueprintType)
enum class EMTAMissionFailReason : uint8
{
	None,
	TimeUp,
	Wrecked,
	TargetLost,
	Detected,
	Busted,
	Died,
	CargoDamaged,
	PassengerLeft,
	HeatCap,
	LastPlace,
	Aborted
};

/** Art eines Kartenpunkts fuer Marker und Minimap. */
UENUM(BlueprintType)
enum class EMTAMissionMarkerKind : uint8
{
	Objective      UMETA(DisplayName = "Aktuelles Ziel"),
	NextObjective  UMETA(DisplayName = "Naechstes Ziel"),
	Target         UMETA(DisplayName = "Zielfahrzeug"),
	Escort         UMETA(DisplayName = "Begleitfahrzeug"),
	Opponent       UMETA(DisplayName = "Gegner"),
	SafeZone       UMETA(DisplayName = "Versteck")
};

/** Ein Schritt einer Mission (Reihenfolge = Spielreihenfolge). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAMissionStep
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	EMTAMissionStepKind Kind = EMTAMissionStepKind::Reach;

	/** Zielort (Weltposition, Z = Boden). Bei Chase: Treffpunkt/Startpunkt des Zielfahrzeugs. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	FVector Location = FVector::ZeroVector;

	/** Ankunftsradius (cm, horizontal). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	float Radius = 900.f;

	/** Pickup/Deliver: Stillstandszeit (s). HoldZone: benoetigte Zeit im Bereich (s). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	float Duration = 0.f;

	/** Zieltext fuer das HUD ("Liefere das Paket an: Core Tower Plaza"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	FString Label;

	/** Aufgeloester Ortsname (Anzeige). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	FString PlaceName;

	/** Zaehlt fuer die Fortschrittsanzeige ("2/4 Checkpoints"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	bool bCountsProgress = false;

	/** Zeitlimit beginnt beim Betreten dieses Schritts (sonst ab Missionsstart, wenn kein Schritt das Flag traegt). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	bool bStartsTimer = false;

	/** Nach diesem Schritt ist Fracht/Fahrgast an Bord (Schadens- und Aussteigekontrolle). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	bool bLoadsCargo = false;

	/** Nach diesem Schritt ist die Fracht abgeliefert. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	bool bUnloadsCargo = false;

	/** Zu Fuss per IMTAInteractable ausloesbar (Terminal, Paket). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	bool bInteractive = false;

	// --- Chase -------------------------------------------------------------------------------

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Chase")
	EMTAChaseCaptureMode CaptureMode = EMTAChaseCaptureMode::Ram;

	/** 0..1: Fluchttalent (Tempo und Abbiegeentscheidungen weg vom Spieler). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Chase")
	float EscapeSkill = 0.5f;

	/** Begleitfahrzeuge, die dem Ziel folgen (params.reinforcements). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Chase")
	int32 Escorts = 0;

	/** Beschatten: Mindest- und Hoechstabstand (cm) und Dauer bis zur Ankunft des Ziels (s). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Chase")
	float ObserveMinDistance = 2500.f;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Chase")
	float ObserveMaxDistance = 12000.f;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Chase")
	float ObserveDuration = 120.f;

	/** Anzeigename des Ziels ("Aurelia Labor Truck"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Chase")
	FString TargetName;

	// --- LoseHeat ----------------------------------------------------------------------------

	/** Fahndung beim Betreten mindestens auf diesen Wert setzen (0 = nicht anheben). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Escape")
	float HeatLevel = 0.f;

	/** Verstecke: Erreichen beendet die Flucht sofort (Fahndung -> 0). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Escape")
	TArray<FVector> SafeZones;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission|Escape")
	float SafeZoneRadius = 1500.f;
};

/** Vollstaendig aufgeloeste, spielbare Mission. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAMissionSpec
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString Title;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString Text;
	/** Roher Typname aus story.json ("courier"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString TypeName;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") EMTAMissionType Type = EMTAMissionType::Unknown;
	/** Bezirks-ID ("saltmarket"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString District;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString Giver;
	/** Anzeigetext der Belohnung (Feld "reward" oder "$ <Geld>"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString RewardText;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") bool bMain = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") bool bFromStory = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 Act = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FIntPoint Cell = FIntPoint(4, 4);

	/** Gesamtes Zeitlimit (s), 0 = keins. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") float TimeLimit = 0.f;
	/** Erlaubter Fahrzeugschaden an der Fracht (Anteil 0..1 der Fahrzeuggesundheit), 1 = nur Totalschaden zaehlt. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") float DamageLimit = 1.f;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") bool bFragile = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 Passengers = 0;
	/** Fahndung beim Start (params.heat_start / heat_increase), 0 = keine. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") float HeatStart = 0.f;
	/** Hoechstfahndung (params.heat_cap), < 0 = keine Grenze. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") float HeatCap = -1.f;

	// Rennen
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 Opponents = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 Laps = 1;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 Stake = 0;
	/** Index des ersten Rennschritts (Startlinie) und des letzten (Ziel); -1 = kein Rennen. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 RaceFirstStep = -1;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 RaceLastStep = -1;

	/** Basisgeld = Summe der "money"-Effekte in onSuccess (Story zahlt es selbst aus). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 BaseMoney = 0;
	/** Bezirksfaktor laut MISSIONSTYPEN.md. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") float DistrictFactor = 1.f;
	/** Substantiv fuer die Fortschrittsanzeige ("Checkpoints", "Stationen"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString ProgressNoun;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") TArray<FMTAMissionStep> Steps;

	/** Optionales "onFail"-Array aus story.json als JSON-Text (wird ueber ApplyEffectsJson angewandt). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString OnFailJson;
	/** Rohes params-Objekt als JSON-Text (Debug). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString ParamsJson;

	bool IsValid() const { return !Id.IsEmpty() && Steps.Num() > 0; }
};

/** Ein Eintrag der Auftragsauswahl. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAMissionOffer
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString Id;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString Title;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") EMTAMissionType Type = EMTAMissionType::Unknown;
	/** Anzeigename des Typs ("Verfolgung"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString TypeDisplay;
	/** Anzeigename des Bezirks ("Saltmarket"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString DistrictDisplay;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString RewardText;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 BaseMoney = 0;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") bool bMain = false;
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 Act = 0;
	/** Entfernung Spieler -> Auftraggeber (m), -1 ohne Spieler. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") int32 DistanceMeters = -1;
	/** Fertige Zeile fuer die Liste ("[1] HAUPT  Rueckkehr  - Kurier, Route 9, $200"). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission") FString DisplayLine;
};

/** Kartenpunkt fuer Marker/Minimap. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAMissionMarker
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	EMTAMissionMarkerKind Kind = EMTAMissionMarkerKind::Objective;

	/** Radius des Zielbereichs (cm), 0 = Punkt. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Mission")
	float Radius = 0.f;
};

/** Zielinhalt geaendert (neuer Schritt, neuer Zustand). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FMTAMissionObjectiveSignature);

/** Mission beendet (Erfolg oder Fehlschlag/Abbruch). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAMissionFinishedSignature, bool, bSuccess);

/** Runner-Zustand geaendert. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FMTAMissionRunStateSignature, EMTAMissionRunState, NewState);
