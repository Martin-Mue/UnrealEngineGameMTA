#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MTAChaosTypes.generated.h"

/**
 * Datentypen der Chaos-Systemik (siehe docs/ZU_FUSS_UND_SYSTEMIK.md).
 * Alles hier ist bewusst reine Daten: Polizei-KI, HUD, Radio und Story binden nur
 * an die Delegates von UMTAChaosSubsystem und lesen diese Strukturen.
 */

/** Art eines Verbrechens/Ereignisses. Reihenfolge ist Teil der Datenschnittstelle (DataTable-Zeilen). */
UENUM(BlueprintType)
enum class EMTACrimeType : uint8
{
	None              UMETA(DisplayName = "Keins"),
	Speeding          UMETA(DisplayName = "Raserei"),
	RecklessDriving   UMETA(DisplayName = "Gefaehrliches Fahren"),
	Collision         UMETA(DisplayName = "Unfall"),
	HitAndRun         UMETA(DisplayName = "Fahrerflucht"),
	PropertyDamage    UMETA(DisplayName = "Sachschaden"),
	PedestrianHit     UMETA(DisplayName = "Fussgaenger angefahren"),
	VehicleTheft      UMETA(DisplayName = "Fahrzeugdiebstahl"),
	Carjacking        UMETA(DisplayName = "Fahrzeugraub"),
	BreakIn           UMETA(DisplayName = "Aufbruch"),
	Assault           UMETA(DisplayName = "Koerperverletzung"),
	Trespass          UMETA(DisplayName = "Hausfriedensbruch"),
	EvidenceTampering UMETA(DisplayName = "Beweismanipulation"),
	CameraTampering   UMETA(DisplayName = "Aegis-Sabotage"),
	PoliceContact     UMETA(DisplayName = "Polizeikontakt"),
	Count             UMETA(Hidden)
};

/** Wie viel die Behoerden ueber ein Verbrechen wissen (steigt nur, sinkt nie). */
UENUM(BlueprintType)
enum class EMTACrimeKnowledge : uint8
{
	Unknown   UMETA(DisplayName = "Unbemerkt"),
	Witnessed UMETA(DisplayName = "Gesehen (noch nicht gemeldet)"),
	Reported  UMETA(DisplayName = "Gemeldet"),
	Recorded  UMETA(DisplayName = "Von Aegis erfasst")
};

/** Aegis-Kameraprofil (Konzept 8.2: Dichte und Art der Ueberwachung je Bezirk). */
UENUM(BlueprintType)
enum class EMTAAegisTier : uint8
{
	Street      UMETA(DisplayName = "Strasse (Core-Standard)"),
	PrivateGate UMETA(DisplayName = "Privat/Tor (Greenmoor)"),
	Advertising UMETA(DisplayName = "Werbung/Gesicht (Sunstrip)"),
	Checkpoint  UMETA(DisplayName = "Checkpoint (Route 9)"),
	Derelict    UMETA(DisplayName = "Verwahrlost (Rustline/Lantern Hill)")
};

/** Quelle einer Sichtung (fuer die Polizei: "zuletzt gesehen"). */
UENUM(BlueprintType)
enum class EMTASightingSource : uint8
{
	Witness     UMETA(DisplayName = "Zeuge"),
	AegisCamera UMETA(DisplayName = "Aegis-Kamera"),
	Police      UMETA(DisplayName = "Polizei"),
	Noise       UMETA(DisplayName = "Geraeusch")
};

/**
 * Ein Verbrechen/Chaos-Ereignis. Wird von Spielsystemen erzeugt (Fahrzeugeinstieg,
 * Kollisionen, Interaktionen, Missionen) und im UMTAChaosSubsystem gesammelt.
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTACrimeEvent
{
	GENERATED_BODY()

	/** Vom Subsystem vergeben (fortlaufend). INDEX_NONE = noch nicht registriert. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	int32 CrimeId = INDEX_NONE;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	EMTACrimeType Type = EMTACrimeType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	FVector Location = FVector::ZeroVector;

	/** Bezirk; leer = Subsystem bestimmt ihn ueber den Bezirks-Resolver. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	FName DistrictId = NAME_None;

	/** Weltzeit (s) bei Tat; vom Subsystem gesetzt. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	float WorldTime = 0.f;

	/** Taeter (Spielerfigur oder Fahrzeug, in dem sie sitzt). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	TWeakObjectPtr<AActor> Perpetrator;

	/** Opfer (NPC, Fahrzeug, Prop) - Opfer sind immer Zeugen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	TWeakObjectPtr<AActor> Victim;

	/** Fahrzeug des Taeters, falls vorhanden (fuer Kennzeichen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	TWeakObjectPtr<AActor> PerpetratorVehicle;

	/** Kennzeichen zur Tatzeit (vom Subsystem aus dem Fahrzeugregister gefuellt). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	FName PlateId = NAME_None;

	/** Erscheinungsbild des Taeters zur Tatzeit (Kleidungs-ID aus UMTAStealthComponent). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	FName OutfitId = NAME_None;

	/** Schwere 0..2 (1 = normal). Skaliert Heat, Ruf und Aegis-Folgen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos", meta = (ClampMin = "0", ClampMax = "2"))
	float Severity = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	EMTACrimeKnowledge Knowledge = EMTACrimeKnowledge::Unknown;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	int32 WitnessCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	bool bPlateCaptured = false;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	bool bFaceCaptured = false;

	/** Heat, die bereits ueber UMTAHeatSubsystem gebucht wurde (verhindert Doppelbuchung). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	float HeatApplied = 0.f;

	/** Freie Zusatzinformationen (Missions-ID, Fraktion, ...). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	TArray<FName> Tags;

	bool IsValid() const { return CrimeId != INDEX_NONE && Type != EMTACrimeType::None; }
};

/**
 * Regel fuer einen Verbrechenstyp. Als DataTable-Zeile nutzbar (Zeilenname = Enum-Name,
 * z. B. "VehicleTheft"); ohne DataTable gelten die Code-Standardwerte aus
 * UMTAChaosSubsystem::MakeDefaultRule.
 */
USTRUCT(BlueprintType)
struct MTA6_API FMTACrimeRule : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	EMTACrimeType Type = EMTACrimeType::None;

	/** Heat (0..5-Skala des HeatSubsystems), die bei Meldung/Erfassung gebucht wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	float Heat = 0.5f;

	/** true = Heat sofort (z. B. Polizeikontakt); false = erst nach Zeugenmeldung oder Aegis-Erfassung. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	bool bImmediateHeat = false;

	/** Aenderung des Bezirks-Rufs bei Meldung (negativ = schlechter). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	float ReputationDelta = -2.f;

	/** Aegis-Profil-Zuwachs (0..100), wenn Kennzeichen oder Gesicht erfasst wird. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	float AegisDelta = 3.f;

	/** Radius (cm), in dem Zeugen die Tat bemerken koennen. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	float NoticeRadius = 3000.f;

	/** Lautstaerke 0..1: Zeugen ohne Sichtlinie bemerken laute Taten im halben Radius. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos", meta = (ClampMin = "0", ClampMax = "1"))
	float Loudness = 0.3f;

	/** Meldeverzoegerung (s) eines Zeugen: zufaellig zwischen Min und Max. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	float ReportDelayMin = 8.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	float ReportDelayMax = 25.f;

	/** Wie lange Aegis-Kameras das Verbrechen noch einem vorbeifahrenden Kennzeichen zuordnen (s). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	float CameraAttributionWindow = 90.f;

	/** Satire/Radio: Schlagzeilen-Schluessel fuer Radio-Meldungen (Lokalisierung). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Chaos")
	FName NewsKey = NAME_None;
};

/** Registereintrag eines Fahrzeugs (Kennzeichen-System, Konzept 8.2a). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAVehicleRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	TWeakObjectPtr<AActor> Vehicle;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	FName PlateId = NAME_None;

	/** Gehoert dem Spieler (Garage, gekauft). Stehlen loest dann keine Straftat aus. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	bool bPlayerOwned = false;

	/** Vom Spieler gestohlen. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	bool bStolen = false;

	/** "Heiss": Kennzeichen ist zur Fahndung ausgeschrieben (Aegis meldet jede Sichtung). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	bool bHot = false;
};

/** Eintrag im Gedaechtnis eines Zeugen. */
USTRUCT(BlueprintType)
struct MTA6_API FMTAWitnessMemory
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	int32 CrimeId = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	EMTACrimeType Type = EMTACrimeType::None;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	FVector Location = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	TWeakObjectPtr<AActor> Perpetrator;

	/** Gemerkte Kleidung/Kennzeichen (Wiedererkennung scheitert nach Wechsel). */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	FName RememberedOutfit = NAME_None;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	FName RememberedPlate = NAME_None;

	/** Sicherheit 0..1 (Entfernung, Licht, Sichtdauer). Verfaellt mit der Zeit. */
	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	float Confidence = 1.f;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	float TimeSeen = 0.f;

	UPROPERTY(BlueprintReadOnly, Category = "MTA6|Chaos")
	bool bReported = false;
};

/** Aegis-Erfassungsprofil einer Kamera (datengetrieben, je Kamera ueberschreibbar). */
USTRUCT(BlueprintType)
struct MTA6_API FMTAAegisCaptureProfile
{
	GENERATED_BODY()

	/** Grundchance je Scan, ein Kennzeichen im Kegel zu lesen (0..1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis", meta = (ClampMin = "0", ClampMax = "1"))
	float BaseChance = 0.45f;

	/** Multiplikator bei Aegis-Profil 0 bzw. 100 (linear dazwischen). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float ProfileMultiplierAtZero = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float ProfileMultiplierAtMax = 1.6f;

	/** Pro km/h ueber 50 km/h sinkt die Chance um diesen Anteil (Bewegungsunschaerfe). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float SpeedPenaltyPerKmh = 0.004f;

	/** Kann zu Fuss Gesichter erkennen (Sunstrip-Werbekameras, Checkpoints). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	bool bFaceRecognition = false;

	/** Tempomessung: erfasst Raserei ab dieser Geschwindigkeit (0 = aus). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	float SpeedLimitKmh = 0.f;

	/** Meldet jedes heisse Kennzeichen sofort als Sichtung an die Polizei. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MTA6|Aegis")
	bool bReportsHotPlates = true;
};
